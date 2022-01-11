#include "libmcu/cli.h"
#include <stdbool.h>
#include <string.h>
#include "libmcu/assert.h"

#define CLI_PROMPT				"$ "
#define CLI_PROMPT_OK				""
#define CLI_PROMPT_ERROR			"ERROR\n"
#define CLI_PROMPT_NOT_FOUND			"command not found\n"
#define CLI_PROMPT_START_MESSAGE		\
	"\n\nType 'help' to get a list of available commands.\n"
#define CLI_PROMPT_EXIT_MESSAGE			"EXIT\n"

static bool readline(cli_t *cli)
{
	char ch;

	if (cli->io->read(&ch, 1) != 1) {
		return false;
	}

	if (ch == '\r' || ch == '\n') {
		cli->io->write("\n", 1);

		cli->cmdbuf[cli->cmdbuf_index++] = '\n';
		cli->cmdbuf[cli->cmdbuf_index++] = '\0';

		cli->cmdbuf_index = 0;

		return true;
	} else if (ch == '\b') {
		if (cli->cmdbuf_index > 0) {
			cli->cmdbuf_index--;
			cli->io->write("\b \b", 3);
		}
	} else if (ch == '\t') {
	} else {
		if (cli->cmdbuf_index >= CLI_COMMAND_MAXLEN) {
			return false;
		}

		cli->cmdbuf[cli->cmdbuf_index++] = ch;
		cli->io->write(&ch, 1);
	}

	return false;
}

static int parse_command(char *str, const char *argv[], size_t maxargs)
{
	int argc = 0;
	char *p = str;
	argv[0] = str;
	while ((p = strpbrk(p + 1, " \n")) != NULL) {
		argc += 1;
		if ((size_t)argc < maxargs) {
			argv[argc] = p + 1;
		}
		*p = '\0';
	}

	return (size_t)argc > maxargs? (int)maxargs : argc;
}

static void report_result(const cli_io_t *io, cli_cmd_error_t err,
		const cli_cmd_t *cmd)
{
	switch (err) {
	case CLI_CMD_SUCCESS:
		io->write(CLI_PROMPT_OK, strlen(CLI_PROMPT_OK));
		break;
	case CLI_CMD_INVALID_PARAM:
		if (cmd && cmd->desc) {
			io->write(cmd->desc, strlen(cmd->desc));
			io->write("\n", 1);
		}
		break;
	case CLI_CMD_NOT_FOUND:
		io->write(CLI_PROMPT_NOT_FOUND, strlen(CLI_PROMPT_NOT_FOUND));
		break;
	case CLI_CMD_ERROR:
		io->write(CLI_PROMPT_ERROR, strlen(CLI_PROMPT_ERROR));
		break;
	default:
		break;
	}
}

static cli_cmd_error_t process_command(const cli_t *cli,
		int argc, const char *argv[], const void *env)
{
	if (argc <= 0) {
		return CLI_CMD_BLANK;
	}

	cli_cmd_error_t rc = CLI_CMD_NOT_FOUND;
	const cli_cmd_t *cmd = NULL;

	for (size_t i = 0; i < cli->cmds_count; i++) {
		cmd = &cli->cmds[i];
		if (strcmp(cmd->name, argv[0]) == 0) {
			rc = cmd->func(argc, argv, env);
			break;
		}
	}

	report_result(cli->io, rc, cmd);

	return rc;
}

static cli_cmd_error_t cli_step_core(cli_t *cli)
{
	if (!readline(cli)) {
		return CLI_CMD_SUCCESS;
	}

	const char *argv[CLI_ARGS_MAXLEN];
	int argc = parse_command(cli->cmdbuf, argv, CLI_ARGS_MAXLEN);

	cli_cmd_error_t err = process_command(cli, argc, argv, cli);

	if (err != CLI_CMD_EXIT) {
		cli->io->write(CLI_PROMPT, strlen(CLI_PROMPT));
	}

	return err;
}

void cli_step(cli_t *cli)
{
	cli_step_core(cli);
}

void cli_run(cli_t *cli)
{
	cli_cmd_error_t rc;

	do {
		rc = cli_step_core(cli);
	} while (rc != CLI_CMD_EXIT);

	cli->io->write(CLI_PROMPT_EXIT_MESSAGE,
			strlen(CLI_PROMPT_EXIT_MESSAGE));
}

void cli_init(cli_t *cli,
		const cli_io_t *io, const cli_cmd_t *cmds, size_t cmdcnt)
{
	assert(cli != NULL && io != NULL && cmds != NULL);

	cli->io = io;
	cli->cmds = cmds;
	cli->cmds_count = cmdcnt;
	cli->cmdbuf_index = 0;

	io->write(CLI_PROMPT_START_MESSAGE, strlen(CLI_PROMPT_START_MESSAGE));
	io->write(CLI_PROMPT, strlen(CLI_PROMPT));
}