# Mission: Secure ldcx.tech deployments

## Why
Replace the plaintext root password used for VibeBoard Companion releases with a dedicated SSH deployment key, so routine publishing no longer depends on a readable desktop credential file.

## Success looks like
- Authenticate to `ldcx.tech` with a passphrase-protected deployment key.
- Run the bounded Companion upload workflow without password fallback.
- Preserve an emergency credential outside the repository and remove the desktop plaintext file.

## Constraints
- Keep the existing root account and server paths for this migration.
- Do not change nginx, SSH daemon configuration, or the root password during the first migration step.
- Never store private keys or passwords in the repository.

## Out of scope
- Replacing root deployment with a dedicated least-privilege server account.
- Disabling password authentication server-wide.

