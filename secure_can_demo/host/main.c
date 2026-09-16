// SPDX-License-Identifier: BSD-2-Clause
#define _GNU_SOURCE
#include <errno.h>
#include <inttypes.h>
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <net/if.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <linux/can.h>
#include <linux/can/raw.h>

#include <tee_client_api.h>
#include <secure_can_ta.h>

#define SECURE_CAN_FRAME_COUNT   5U
#define SECURE_CAN_DEFAULT_IFACE "can0"

/* The CAN/Secure World pipeline runs on this core; a fully independent
 * Normal World worker (no TEE calls at all) runs concurrently on the
 * other one, to make it visible that a Secure World call only blocks the
 * core that issued it - it does not stop Normal World on other cores.
 * See KNOWLEDGE.md Incident 4: an earlier attempt at this used a TF-A/
 * BL31 boot-flow change to run Secure World with no Normal World call at
 * all, and that bricked boot. This demonstrates the same "independent
 * and parallel" property without touching boot flow at all - ordinary
 * pthreads plus CPU affinity, entirely in userspace.
 */
#define SECURE_CAN_PIPELINE_CPU    0U
#define SECURE_CAN_INDEPENDENT_CPU 1U
#define SECURE_CAN_INDEPENDENT_TICK_MS 300

/* Purely for live demo watchability: the real CAN/TEE work per frame
 * takes only a few ms, which is too fast for a human to see the two
 * cores' output actually interleaving. This adds a deliberate pause
 * after each frame so the whole run takes several seconds on screen.
 */
#define SECURE_CAN_DEMO_PACE_MS 800

/* How long to wait for a frame to come back before giving up. A frame
 * only comes back at all because the interface is in chip-level loopback
 * mode (no second working CAN node exists on this board yet - see
 * KNOWLEDGE.md, Incident 5's third addendum) - so a timeout here almost
 * always means loopback mode isn't enabled on the interface, not a real
 * bus problem.
 */
#define SECURE_CAN_RECV_TIMEOUT_SECONDS 2

static int report_teec_error(const char *operation, TEEC_Result result,
			     uint32_t origin)
{
	fprintf(stderr, "%s failed: result=0x%08" PRIx32
		" origin=0x%08" PRIx32 "\n", operation, result, origin);
	return 1;
}

/* Milliseconds since an arbitrary fixed point, shared by both threads, so
 * their interleaved log lines prove they actually overlapped in time
 * rather than just looking plausible.
 */
static long long monotonic_ms(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static int pin_current_thread_to_cpu(unsigned int cpu)
{
	cpu_set_t set;

	CPU_ZERO(&set);
	CPU_SET(cpu, &set);
	if (pthread_setaffinity_np(pthread_self(), sizeof(set), &set)) {
		perror("pthread_setaffinity_np");
		return 1;
	}
	return 0;
}

static atomic_int independent_worker_keep_running = 1;

/* Runs entirely in Normal World with no TEE calls of its own - its only
 * job is to keep producing its own independent message stream, on its
 * own dedicated core, for as long as the real CAN/Secure World pipeline
 * (running concurrently on the other core) is active. Its timestamps
 * interleaving with the pipeline's own timestamps is the actual proof of
 * parallelism, not just the two being started close together.
 */
static void *independent_normal_world_worker(void *arg)
{
	long long start_ms = *(long long *)arg;
	unsigned int tick = 0;

	if (pin_current_thread_to_cpu(SECURE_CAN_INDEPENDENT_CPU))
		return NULL;

	printf("[core %u] t=%lldms Normal World independent worker started, no Secure World calls\n",
	       SECURE_CAN_INDEPENDENT_CPU, monotonic_ms() - start_ms);

	while (atomic_load(&independent_worker_keep_running)) {
		printf("[core %u] t=%lldms independent Normal World message #%u\n",
		       SECURE_CAN_INDEPENDENT_CPU, monotonic_ms() - start_ms, tick++);
		usleep(SECURE_CAN_INDEPENDENT_TICK_MS * 1000);
	}

	printf("[core %u] t=%lldms Normal World independent worker stopping\n",
	       SECURE_CAN_INDEPENDENT_CPU, monotonic_ms() - start_ms);
	return NULL;
}

/* Opens a raw SocketCAN socket bound to ifname and sets a receive timeout
 * so a hung bus (e.g. loopback mode not enabled) fails fast with a clear
 * error instead of blocking forever.
 */
static int open_can_socket(const char *ifname)
{
	struct sockaddr_can addr;
	struct ifreq ifr;
	struct timeval timeout;
	int sock;

	sock = socket(PF_CAN, SOCK_RAW, CAN_RAW);
	if (sock < 0) {
		perror("socket(PF_CAN)");
		return -1;
	}

	memset(&ifr, 0, sizeof(ifr));
	strncpy(ifr.ifr_name, ifname, IFNAMSIZ - 1);
	if (ioctl(sock, SIOCGIFINDEX, &ifr) < 0) {
		fprintf(stderr, "ioctl(SIOCGIFINDEX, %s): %s\n", ifname,
			strerror(errno));
		close(sock);
		return -1;
	}

	memset(&addr, 0, sizeof(addr));
	addr.can_family = AF_CAN;
	addr.can_ifindex = ifr.ifr_ifindex;

	if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		perror("bind(AF_CAN)");
		close(sock);
		return -1;
	}

	timeout.tv_sec = SECURE_CAN_RECV_TIMEOUT_SECONDS;
	timeout.tv_usec = 0;
	setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

	return sock;
}

/* Deterministic per-index payload so the pipeline stays reproducible run
 * to run, same as before real hardware was available - the difference now
 * is this payload is actually transmitted over real SPI to the MCP2518FD
 * chip and read back over the same real hardware path (via the chip's
 * loopback mode), not just built in memory.
 */
static void fill_frame_payload(uint8_t data[SECURE_CAN_FRAME_DATA_LEN],
			       unsigned int index)
{
	unsigned int i;

	for (i = 0; i < SECURE_CAN_FRAME_DATA_LEN; i++)
		data[i] = (uint8_t)((index * 0x11) + i);
}

static int send_can_frame(int can_sock, uint32_t can_id,
			  const uint8_t data[SECURE_CAN_FRAME_DATA_LEN])
{
	struct can_frame raw;

	memset(&raw, 0, sizeof(raw));
	raw.can_id = can_id;
	raw.can_dlc = SECURE_CAN_FRAME_DATA_LEN;
	memcpy(raw.data, data, SECURE_CAN_FRAME_DATA_LEN);

	if (write(can_sock, &raw, sizeof(raw)) != (ssize_t)sizeof(raw)) {
		perror("write(can socket)");
		return -1;
	}
	return 0;
}

/* Blocks (up to the receive timeout) for the next real data frame on the
 * bus, skipping error/remote frames, and converts it into the project's
 * wire format.
 */
static int receive_can_frame(int can_sock, struct secure_can_frame *frame)
{
	struct can_frame raw;
	ssize_t n;
	uint8_t dlc;

	for (;;) {
		n = read(can_sock, &raw, sizeof(raw));
		if (n != (ssize_t)sizeof(raw)) {
			if (n < 0)
				fprintf(stderr,
					"read(can socket) timed out or failed: %s\n"
					"  (is the interface up with loopback mode enabled? see KNOWLEDGE.md)\n",
					strerror(errno));
			return -1;
		}
		if (raw.can_id & (CAN_ERR_FLAG | CAN_RTR_FLAG))
			continue;

		frame->can_id = raw.can_id & CAN_EFF_MASK;
		memset(frame->data, 0, SECURE_CAN_FRAME_DATA_LEN);
		dlc = raw.can_dlc > SECURE_CAN_FRAME_DATA_LEN ?
			SECURE_CAN_FRAME_DATA_LEN : raw.can_dlc;
		memcpy(frame->data, raw.data, dlc);
		return 0;
	}
}

static void print_frame_result(const struct secure_can_frame *frame,
			       const uint8_t *tag)
{
	unsigned int i;

	printf("Linux: CAN id=0x%03" PRIx32 " data=", frame->can_id);
	for (i = 0; i < SECURE_CAN_FRAME_DATA_LEN; i++)
		printf("%02" PRIx8 " ", frame->data[i]);
	printf("-> Secure World tag=");
	for (i = 0; i < SECURE_CAN_FRAME_TAG_LEN; i++)
		printf("%02" PRIx8, tag[i]);
	puts("  [Linux forwards the authenticated frame]");
}

static int classify_frame(TEEC_Session *session,
			  const struct secure_can_frame *frame,
			  uint32_t *frame_class)
{
	TEEC_Operation operation;
	TEEC_Result result;
	uint32_t origin = 0;

	memset(&operation, 0, sizeof(operation));
	operation.paramTypes = TEEC_PARAM_TYPES(TEEC_MEMREF_TEMP_INPUT,
						TEEC_VALUE_OUTPUT,
						TEEC_NONE, TEEC_NONE);
	operation.params[0].tmpref.buffer = (void *)frame;
	operation.params[0].tmpref.size = sizeof(*frame);

	result = TEEC_InvokeCommand(session, TA_SECURE_CAN_CMD_CLASSIFY_FRAME,
				    &operation, &origin);
	if (result != TEEC_SUCCESS)
		return report_teec_error("CLASSIFY_FRAME", result, origin);

	*frame_class = operation.params[1].value.a;
	return 0;
}

static int authenticate_frame(TEEC_Session *session,
			      const struct secure_can_frame *frame,
			      uint8_t *tag)
{
	TEEC_Operation operation;
	TEEC_Result result;
	uint32_t origin = 0;

	memset(&operation, 0, sizeof(operation));
	operation.paramTypes = TEEC_PARAM_TYPES(TEEC_MEMREF_TEMP_INPUT,
						 TEEC_MEMREF_TEMP_OUTPUT,
						 TEEC_NONE, TEEC_NONE);
	operation.params[0].tmpref.buffer = (void *)frame;
	operation.params[0].tmpref.size = sizeof(*frame);
	operation.params[1].tmpref.buffer = tag;
	operation.params[1].tmpref.size = SECURE_CAN_FRAME_TAG_LEN;

	result = TEEC_InvokeCommand(session, TA_SECURE_CAN_CMD_PROCESS_FRAME,
				    &operation, &origin);
	if (result != TEEC_SUCCESS)
		return report_teec_error("PROCESS_FRAME", result, origin);

	return 0;
}

static int verify_frame(TEEC_Session *session,
			const struct secure_can_frame *frame,
			const uint8_t *tag, int *accepted)
{
	TEEC_Operation operation;
	TEEC_Result result;
	uint32_t origin = 0;

	memset(&operation, 0, sizeof(operation));
	operation.paramTypes = TEEC_PARAM_TYPES(TEEC_MEMREF_TEMP_INPUT,
						TEEC_MEMREF_TEMP_INPUT,
						TEEC_VALUE_OUTPUT, TEEC_NONE);
	operation.params[0].tmpref.buffer = (void *)frame;
	operation.params[0].tmpref.size = sizeof(*frame);
	operation.params[1].tmpref.buffer = (void *)tag;
	operation.params[1].tmpref.size = SECURE_CAN_FRAME_TAG_LEN;

	result = TEEC_InvokeCommand(session, TA_SECURE_CAN_CMD_VERIFY_FRAME,
				    &operation, &origin);
	if (result != TEEC_SUCCESS)
		return report_teec_error("VERIFY_FRAME", result, origin);

	*accepted = (int)operation.params[2].value.a;
	return 0;
}

/* Secure World only reacts to calls Normal World makes, it never runs on
 * its own timer or interrupt - Linux drives the pipeline by transmitting
 * each frame over the real can0 hardware, reading it back over that same
 * hardware (via the chip's loopback mode), and only then handing it to
 * Secure World. Every frame is classified first, and only frames Secure
 * World marks SECURE pay the cost of full authentication — NORMAL frames
 * are handled directly in Normal World, same as a real gateway wouldn't
 * waste secure-world time on low-priority traffic.
 */
static int run_can_frame_pipeline(TEEC_Session *session, int can_sock,
				  long long start_ms)
{
	struct secure_can_frame last_secure_frame;
	uint8_t last_secure_tag[SECURE_CAN_FRAME_TAG_LEN];
	unsigned int i;
	int have_secure_frame = 0;
	int accepted = 0;

	printf("[core %u] t=%lldms Linux (Normal World) starting live CAN message pipeline over real hardware\n",
	       SECURE_CAN_PIPELINE_CPU, monotonic_ms() - start_ms);

	for (i = 0; i < SECURE_CAN_FRAME_COUNT; i++) {
		struct secure_can_frame frame;
		uint8_t payload[SECURE_CAN_FRAME_DATA_LEN];
		uint32_t frame_class;

		fill_frame_payload(payload, i);
		if (send_can_frame(can_sock, 0x100 + i, payload))
			return 1;
		if (receive_can_frame(can_sock, &frame))
			return 1;

		if (classify_frame(session, &frame, &frame_class))
			return 1;

		if (frame_class == SECURE_CAN_CLASS_SECURE) {
			uint8_t tag[SECURE_CAN_FRAME_TAG_LEN];

			printf("[core %u] t=%lldms CAN id=0x%03" PRIx32
			       " classified SECURE by Secure World, routing for authentication\n",
			       SECURE_CAN_PIPELINE_CPU, monotonic_ms() - start_ms,
			       frame.can_id);
			if (authenticate_frame(session, &frame, tag))
				return 1;

			print_frame_result(&frame, tag);
			last_secure_frame = frame;
			memcpy(last_secure_tag, tag, sizeof(last_secure_tag));
			have_secure_frame = 1;
		} else {
			printf("[core %u] t=%lldms CAN id=0x%03" PRIx32
			       " classified NORMAL by Secure World, handled directly in Normal World\n",
			       SECURE_CAN_PIPELINE_CPU, monotonic_ms() - start_ms,
			       frame.can_id);
		}

		usleep(SECURE_CAN_DEMO_PACE_MS * 1000);
	}

	puts("PASS: Linux-controlled real CAN frames classified and routed by Secure World");

	if (!have_secure_frame) {
		fprintf(stderr,
			"FAIL: no frame was classified SECURE, nothing to verify\n");
		return 1;
	}

	puts("Linux: re-verifying the last SECURE frame already received from the bus");
	if (verify_frame(session, &last_secure_frame, last_secure_tag, &accepted))
		return 1;
	if (!accepted) {
		fprintf(stderr, "FAIL: Secure World rejected an authentic frame\n");
		return 1;
	}
	puts("PASS: Secure World accepted the authentic frame");

	puts("Linux: simulating a tampered frame on the bus (one data byte flipped)");
	last_secure_frame.data[0] ^= 0xff;
	if (verify_frame(session, &last_secure_frame, last_secure_tag, &accepted))
		return 1;
	if (accepted) {
		fprintf(stderr, "FAIL: Secure World accepted a tampered frame\n");
		return 1;
	}
	puts("PASS: Secure World rejected the tampered frame");

	return 0;
}

int main(int argc, char *argv[])
{
	const char *ifname = (argc > 1) ? argv[1] : SECURE_CAN_DEFAULT_IFACE;
	TEEC_UUID uuid = TA_SECURE_CAN_UUID;
	TEEC_Context context;
	TEEC_Session session;
	TEEC_Operation operation;
	TEEC_Result result;
	uint32_t origin = 0;
	int status = 1;
	int can_sock;
	pthread_t independent_thread;
	int independent_started = 0;
	long long start_ms;
	int pipeline_result;

	if (pin_current_thread_to_cpu(SECURE_CAN_PIPELINE_CPU))
		return 1;

	can_sock = open_can_socket(ifname);
	if (can_sock < 0) {
		fprintf(stderr, "FAIL: could not open CAN interface '%s'\n",
			ifname);
		return 1;
	}

	result = TEEC_InitializeContext(NULL, &context);
	if (result != TEEC_SUCCESS) {
		close(can_sock);
		return report_teec_error("TEEC_InitializeContext", result, origin);
	}

	result = TEEC_OpenSession(&context, &session, &uuid, TEEC_LOGIN_PUBLIC,
				  NULL, NULL, &origin);
	if (result != TEEC_SUCCESS) {
		report_teec_error("TEEC_OpenSession", result, origin);
		goto finalize;
	}

	memset(&operation, 0, sizeof(operation));
	operation.paramTypes = TEEC_PARAM_TYPES(TEEC_VALUE_INOUT, TEEC_NONE,
						TEEC_NONE, TEEC_NONE);
	operation.params[0].value.a = SECURE_CAN_CHALLENGE;
	printf("Normal World challenge: 0x%08" PRIx32 "\n",
	       operation.params[0].value.a);

	result = TEEC_InvokeCommand(&session,
			TA_SECURE_CAN_CMD_PROCESS_CHALLENGE, &operation, &origin);
	if (result != TEEC_SUCCESS) {
		report_teec_error("TEEC_InvokeCommand", result, origin);
		goto close;
	}

	printf("Secure World response: 0x%08" PRIx32 "\n",
	       operation.params[0].value.a);
	printf("Secure World marker:   0x%08" PRIx32 " (SCAN)\n",
	       operation.params[0].value.b);

	if (operation.params[0].value.a != SECURE_CAN_RESPONSE ||
	    operation.params[0].value.b != SECURE_CAN_MARKER) {
		fprintf(stderr, "FAIL: Secure World returned unexpected data\n");
		goto close;
	}

	puts("PASS: project-specific Normal World to Secure World round trip");

	start_ms = monotonic_ms();
	atomic_store(&independent_worker_keep_running, 1);
	if (pthread_create(&independent_thread, NULL,
			   independent_normal_world_worker, &start_ms)) {
		perror("pthread_create");
		goto close;
	}
	independent_started = 1;

	pipeline_result = run_can_frame_pipeline(&session, can_sock, start_ms);

	atomic_store(&independent_worker_keep_running, 0);
	pthread_join(independent_thread, NULL);
	independent_started = 0;

	if (pipeline_result)
		goto close;

	status = 0;

close:
	if (independent_started) {
		atomic_store(&independent_worker_keep_running, 0);
		pthread_join(independent_thread, NULL);
	}
	TEEC_CloseSession(&session);
finalize:
	TEEC_FinalizeContext(&context);
	close(can_sock);
	return status;
}
