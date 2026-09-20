# Security policy

Report certificate-validation bypasses, redirect-policy escapes, credential
leaks and malformed-response memory issues privately to the maintainer before
public disclosure. Do not publish real credentials or private endpoints.

Only the current default branch and latest release are supported. Builds using
OpenSSL or a CA bundle whose hash differs from `DEPENDENCIES.lock` are explicitly
unsupported and must not be released.
