#pragma once
// ---------------------------------------------------------------------------
// CertPin — SHA-256 certificate fingerprint for TLS pinning.
//
// The loader will refuse to proceed if the server's certificate fingerprint
// does not match SPARKY_CERT_PIN, making MITM attacks (Fiddler, Charles,
// mitmproxy, custom CA injection) impossible even if the attacker has
// installed a trusted root CA on the target machine.
//
// HOW TO GET THE PIN FOR YOUR CERTIFICATE
// ----------------------------------------
// After generating sparky.crt (see server README), run:
//
//   openssl x509 -in sparky.crt -fingerprint -sha256 -noout \
//     | sed 's/.*=//' | tr -d ':' | tr 'A-F' 'a-f'
//
// Paste the resulting 64-character lowercase hex string below, then
// rebuild and redistribute the loader binary.
//
// WHAT TO PIN
// -----------
// We pin the full certificate digest (not just the public key).  For a
// self-signed cert that never changes keys this is equivalent and simpler.
// If you rotate the key pair, regenerate the cert AND update this pin.
//
// DEV MODE
// --------
// Leave SPARKY_CERT_PIN as an empty string to disable pinning.
// The loader will warn but still connect.  Never ship with an empty pin.
// ---------------------------------------------------------------------------

// TODO: paste the 64-char SHA-256 fingerprint of sparky.crt here.
static constexpr const char* SPARKY_CERT_PIN =
    "";  // e.g. "a1b2c3d4e5f6...64chars"
