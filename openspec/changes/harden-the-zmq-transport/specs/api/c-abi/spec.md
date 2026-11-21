# Spec Delta

## ADDED Requirements

### Requirement: A transport built through the ABI is encrypted unless plainly asked otherwise

A transport created through the C ABI MUST encrypt its links. Plaintext MUST be something the caller asks for explicitly, never
the result of an omitted argument, and a caller that provides no key material MUST get an error rather than a quiet plaintext
link. The C surface MUST be able to read the CURVE material from a keystore, so a language binding has no reason to skip it.

#### Scenario: A binding that passes a keystore gets encryption

- **WHEN** a language binding loads a keystore through the ABI and builds a transport
- **THEN** the resulting links are CURVE-encrypted and the peer keys come from that keystore

#### Scenario: Missing keys is an error, not a silent downgrade

- **WHEN** a caller builds a transport without key material and without asking for plaintext
- **THEN** the call fails with a status naming the problem, and no socket is opened

#### Scenario: Plaintext is explicit and visible

- **WHEN** a caller asks for a plaintext transport for a loopback test or a diagnostic
- **THEN** the request names plaintext, and the resulting transport reports that its links are unencrypted
