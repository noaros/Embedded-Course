# Demo signing key

`demo_ed25519.pem` signs the Lab 10 firmware images. **It is a demo key and its
private half is public** (it's in this repository). Anyone can sign images the
Lab 10 bootloader accepts. That's fine for a course and unacceptable for a
product, where the private key lives in an HSM or a cloud KMS and the build
server asks it for signatures.

Make your own pair for the lab with:

```sh
python3 tools/fw_sign.py keygen --key tools/keys/my_ed25519.pem \
    --header modules/10-bootloaders-updates/lab/common/pubkey.h
```
