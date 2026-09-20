# WaffleHouse-Client 5.5 Secure UX Update

This update simplifies IM/chat security without changing the CPX cryptographic method.

- **Secure** starts the existing CPX handshake.
- The first successfully authenticated peer fingerprint is automatically pinned (TOFU).
- Future fingerprint changes are rejected and shown as a trust mismatch.
- **Unsecure** closes the existing secure session and returns that conversation to plaintext.
- Secure rooms continue using the existing XChaCha20-Poly1305 room encryption and CPX-encrypted room-key distribution.
- Secure file transfer continues using the existing CPX encryption/authentication.
- Fingerprints remain available through Status / advanced security controls for users who want manual verification.

No cipher, key derivation, handshake frame, encrypted payload format, or room encryption algorithm was replaced by this UX change.
