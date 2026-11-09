// -------------------------------------------------------
// credentials.h
// -------------------------------------------------------

#ifndef CREDENTIALS_H
#define CREDENTIALS_H

// Admin username (plaintext is fine here — it's not secret)
const char* ADMIN_USERNAME = "dahoo";

// Bcrypt hash of your password (cost factor 12)
// Generated offline — raw password is NOT stored anywhere
const char* ADMIN_PASSWORD_HASH = "$2b$12$ux0nH.qgRyIfSaKpldxWDemH7shTfPY0/dDwR.pJ7eGiEL8i2tRpO";

// HMAC secret — used to sign session tokens
// Change this to any random 32+ character string you make up
const char* HMAC_SECRET = "irrig@t10n_s3cr3t_k3y_esp32_2026_kemi";

// Session token validity in seconds (default: 8 hours)
const int SESSION_DURATION_SECONDS = 28800;

#endif