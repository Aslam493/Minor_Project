# OP-TEE Component Revisions

Recorded from the authoritative tree `/home/aslam/Minor_Project/optee_full` on
6 September 2026 after completing Step 6.

| Component | Git revision |
| --- | --- |
| build | `f4419040d1dbce69a5b8e2928132a53b14a2fc56` |
| buildroot | `71d1dddae12e7cbf322d96864d7757f459801f47` |
| firmware | `055e044d5359ded1aacc5a17a8e35365373d0b8b` |
| linux | `8bfce8398f484ece6ce7b13dbab73005bff7405d` |
| optee_client | `ba3dce76046dcaf6cc496ee8a93191029f8090ae` |
| optee_examples | `fa04c03be033eec17dbab1c030e0051152def7f3c` |
| optee_os | `d5001fe0af66a4c059dc6816a7be08d7fb83ff1a` |
| optee_test | `14dff6298c7af399f1e97f558978d3ccac228f3c` |
| trusted-firmware-a | `a1f02f4f3daae7e21ee58b4c93ec3e46b8f28d15` |
| u-boot | `d80bb749fab53da72c4a0e09b8c2d2aaa3103c91` |

All checked component worktrees were clean. Generated build products are
identified by the checksum manifests in `Step5_Verification` and
`Step6_Verification`.

The Step 6 boot artifacts and compressed root filesystem are also committed as
curated recovery outputs even though their parent build tree is ignored. The
full third-party source checkouts, toolchains, caches, and uncompressed build
