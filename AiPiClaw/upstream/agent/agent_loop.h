#pragma once


/**
 * Initialize the agent loop.
 */
int axk_agent_loop_init(void);

/**
 * Start the agent loop task (runs on Core 1).
 * Consumes from inbound queue, calls Claude API, pushes to outbound queue.
 */
int axk_agent_loop_start(void);
