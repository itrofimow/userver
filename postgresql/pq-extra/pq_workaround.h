#pragma once

#include <libpq-fe.h>

#ifdef __cplusplus
extern "C" {
#endif

/// This is a workaroud function for libpq is not handling
/// the `portal suspended` backend response message
extern PGresult* PQXgetResult(PGconn* conn);

/// This is a workaroud function for libpq is not handling
/// the `portal suspended` backend response message
extern int PQXisBusy(PGconn* conn);

/// This is a workaround function for libpq (almost) hard-coding connection
/// read buffer size to 16Kb
extern void PQXenlargeInBuf(PGconn* conn, int new_size);

extern int PQXconsumeInput(PGconn* conn);

extern int PQXinBufSize(PGconn* conn);

#ifdef __cplusplus
}
#endif
