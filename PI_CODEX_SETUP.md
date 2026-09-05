# Connect Raspberry Pi with Laptop and Codex

This guide uses the laptop to run Codex and SSH to work on the Raspberry Pi.

## 1. Connect the hardware

1. Insert the prepared microSD card into the Raspberry Pi.
2. Connect the Raspberry Pi to Ethernet.
3. Connect the 5 V / 2.5 A power supply.
4. Wait about 2 minutes for the Pi to boot.

## 2. Open a terminal on the laptop

The laptop prompt should look similar to:

```text
aslam@Aslam-ideapad:~$
```

## 3. Connect to the Raspberry Pi

Use the current Raspberry Pi IP address:

```bash
ssh aslam@10.237.27.144
```

When connected, the prompt changes to:

```text
aslam@raspberrypi:~$
```

This prompt means commands are now running on the Raspberry Pi.

If the IP address changes, try:

```bash
ssh aslam@raspberrypi.local
```

## 4. Configure the SSH key (one-time setup)

Run these commands from the laptop, not from inside the Pi:

```bash
ls -l ~/.ssh/id_ed25519.pub
ssh-copy-id aslam@10.237.27.144
```

Enter the Raspberry Pi password when requested. Test the key:

```bash
ssh aslam@10.237.27.144
```

If it connects without requesting a password, key authentication works.

Never share or copy the private key file `~/.ssh/id_ed25519`. Only the file ending in `.pub` is public.

## 5. Work on the Pi with Codex

Keep Codex running on the laptop. Ask Codex to perform a task on the Pi, for example:

```text
Using SSH, create a Python program on my Pi and run it.
```

Codex can then use the connection:

```bash
ssh aslam@10.237.27.144
```

The test program created earlier is located at:

```text
/home/aslam/aslam/hello.py
```

Run it from the laptop with one command:

```bash
ssh aslam@10.237.27.144 'python3 /home/aslam/aslam/hello.py'
```

Expected output:

```text
Hello, World!
```

## 6. Useful commands

Show the Pi's IP address:

```bash
hostname -I
```

Check internet access:

```bash
ping -c 4 deb.debian.org
```

Update the package list:

```bash
sudo apt update
```

Leave the Raspberry Pi terminal and return to the laptop:

```bash
exit
```

Shut down the Pi safely before disconnecting power:

```bash
sudo poweroff
```

Wait until the green activity light stops blinking before removing power.

## Troubleshooting

- `No route to host`: verify the Pi is powered on, connected to Ethernet, and still has the same IP address.
- `Connection refused`: verify that SSH is enabled on the Pi.
- `Permission denied`: check the username (`aslam`) and repeat `ssh-copy-id`.
- `Host key verification failed`: do not delete keys blindly; first confirm that the IP still belongs to your Raspberry Pi.
- IITD wired internet may require its designated proxy even when local SSH works.
