# Security

Do not publish account databases, ratings, server secrets, bot tokens, service
environment files, logs, crash/desync dumps, update signing material, or player
configuration.

If you discover a vulnerability or exposed credential, contact the project
owner privately through the contact route listed on
https://loafiieee.com/yule/. Include the affected component, reproduction,
impact, and the smallest useful diagnostic excerpt. Do not include real
passwords, tokens, account records, match authentication keys, or private
addresses.

The online account/control protocol is currently raw TCP and is documented as a
release blocker until TLS with certificate and hostname verification is
complete. Do not assume transport confidentiality on an untrusted network.
