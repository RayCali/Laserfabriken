#ifndef CLI_H
#define CLI_H

/* Print welcome banner and prompt. */
void CLI_Init(void);

/* Poll UART for received bytes and process complete lines.
 * Call from main loop on every iteration (non-blocking). */
void CLI_Process(void);

#endif /* CLI_H */
