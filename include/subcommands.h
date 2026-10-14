/**
 * @file subcommands.h
 * @brief Subcommand entry points for the unified openlogi binary.
 */

#ifndef OPENLOGI_SUBCOMMANDS_H
#define OPENLOGI_SUBCOMMANDS_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Background daemon entry point (openlogid / openlogi daemon).
 */
int daemon_main(int argc, char **argv);

/**
 * @brief CLI control utility entry point (openlogictl / openlogi [status|dpi|...]).
 */
int ctl_main(int argc, char **argv);

/**
 * @brief Diagnostic hardware discovery entry point (openlogi_discover / openlogi discover).
 */
int discover_main(int argc, char **argv);

#ifdef __cplusplus
}
#endif

#endif /* OPENLOGI_SUBCOMMANDS_H */
