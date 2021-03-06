/**
 ******************************************************************************
 * @addtogroup PIOS PIOS Core hardware abstraction layer
 * @{
 * @addtogroup   PIOS_I2C I2C Functions
 * @brief STM32 Hardware dependent I2C functionality
 * @{
 *
 * @file       pios_i2c.c  
 * @author     The OpenPilot Team, http://www.openpilot.org Copyright (C) 2010.
 * @author     Tau Labs, http://taulabs.org, Copyright (C) 2013-2014
 * @brief      I2C Enable/Disable routines
 * @see        The GNU Public License (GPL) Version 3
 * 
 *****************************************************************************/
/* 
 * This program is free software; you can redistribute it and/or modify 
 * it under the terms of the GNU General Public License as published by 
 * the Free Software Foundation; either version 3 of the License, or 
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful, but 
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY 
 * or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License 
 * for more details.
 * 
 * You should have received a copy of the GNU General Public License along 
 * with this program; if not, see <http://www.gnu.org/licenses/>
 */

/* Project Includes */
#include "pios.h"

#if defined(PIOS_INCLUDE_I2C)

#include <pios_i2c_priv.h>

//#define I2C_HALT_ON_ERRORS
#define MAX_I2C_RETRY_COUNT 10

static void go_fsm_fault(struct pios_i2c_adapter *i2c_adapter);
static void go_bus_error(struct pios_i2c_adapter *i2c_adapter);
static void go_stopping(struct pios_i2c_adapter *i2c_adapter);
static void go_stopped(struct pios_i2c_adapter *i2c_adapter);
static void go_starting(struct pios_i2c_adapter *i2c_adapter);
static void go_r_any_txn_addr(struct pios_i2c_adapter *i2c_adapter);
static void go_r_more_txn_pre_one(struct pios_i2c_adapter *i2c_adapter);
static void go_r_any_txn_pre_first(struct pios_i2c_adapter *i2c_adapter);
static void go_r_any_txn_pre_middle(struct pios_i2c_adapter *i2c_adapter);
static void go_r_more_txn_pre_last(struct pios_i2c_adapter *i2c_adapter);
static void go_r_any_txn_post_last(struct pios_i2c_adapter *i2c_adapter);

static void go_r_any_txn_addr(struct pios_i2c_adapter *i2c_adapter);
static void go_r_last_txn_pre_one(struct pios_i2c_adapter *i2c_adapter);
static void go_r_any_txn_pre_first(struct pios_i2c_adapter *i2c_adapter);
static void go_r_any_txn_pre_middle(struct pios_i2c_adapter *i2c_adapter);
static void go_r_last_txn_pre_last(struct pios_i2c_adapter *i2c_adapter);
static void go_r_any_txn_post_last(struct pios_i2c_adapter *i2c_adapter);

static void go_w_any_txn_addr(struct pios_i2c_adapter *i2c_adapter);
static void go_w_any_txn_middle(struct pios_i2c_adapter *i2c_adapter);
static void go_w_more_txn_last(struct pios_i2c_adapter *i2c_adapter);

static void go_w_any_txn_addr(struct pios_i2c_adapter *i2c_adapter);
static void go_w_any_txn_middle(struct pios_i2c_adapter *i2c_adapter);
static void go_w_last_txn_last(struct pios_i2c_adapter *i2c_adapter);

static void go_nack(struct pios_i2c_adapter *i2c_adapter);

struct i2c_adapter_transition {
	void (*entry_fn) (struct pios_i2c_adapter * i2c_adapter);
	enum i2c_adapter_state next_state[I2C_EVENT_NUM_EVENTS];
};

static void i2c_adapter_process_auto(struct pios_i2c_adapter *i2c_adapter);
static void i2c_adapter_inject_event(struct pios_i2c_adapter *i2c_adapter, enum i2c_adapter_event event);
static void i2c_adapter_fsm_init(struct pios_i2c_adapter *i2c_adapter);
static bool i2c_adapter_wait_for_stopped(struct pios_i2c_adapter *i2c_adapter);
static void i2c_adapter_reset_bus(struct pios_i2c_adapter *i2c_adapter);

#if defined(PIOS_I2C_DIAGNOSTICS)
static void i2c_adapter_log_fault(struct pios_i2c_adapter *i2c_adapter, enum pios_i2c_error_type type);
#endif

const static struct i2c_adapter_transition i2c_adapter_transitions[I2C_STATE_NUM_STATES] = {
	[I2C_STATE_FSM_FAULT] = {
				 .entry_fn = go_fsm_fault,
				 .next_state = {
					        [I2C_EVENT_AUTO] = I2C_STATE_STOPPING,
						},
				},
	[I2C_STATE_BUS_ERROR] = {
				 .entry_fn = go_bus_error,
				 .next_state = {
						[I2C_EVENT_AUTO] = I2C_STATE_STOPPING,
						},
				 },

	[I2C_STATE_STOPPED] = {
			       .entry_fn = go_stopped,
			       .next_state = {
					      [I2C_EVENT_START] = I2C_STATE_STARTING,
					      [I2C_EVENT_BUS_ERROR] = I2C_STATE_BUS_ERROR,
					      },
			       },

	[I2C_STATE_STOPPING] = {
				.entry_fn = go_stopping,
				.next_state = {
					       [I2C_EVENT_STOPPED] = I2C_STATE_STOPPED,
					       [I2C_EVENT_BUS_ERROR] = I2C_STATE_BUS_ERROR,
					       },
				},

	[I2C_STATE_STARTING] = {
				.entry_fn = go_starting,
				.next_state = {
					       [I2C_EVENT_STARTED_MORE_TXN_READ] = I2C_STATE_R_MORE_TXN_ADDR,
					       [I2C_EVENT_STARTED_MORE_TXN_WRITE] = I2C_STATE_W_MORE_TXN_ADDR,
					       [I2C_EVENT_STARTED_LAST_TXN_READ] = I2C_STATE_R_LAST_TXN_ADDR,
					       [I2C_EVENT_STARTED_LAST_TXN_WRITE] = I2C_STATE_W_LAST_TXN_ADDR,
					       [I2C_EVENT_NACK] = I2C_STATE_NACK,
					       [I2C_EVENT_BUS_ERROR] = I2C_STATE_BUS_ERROR,
					       },
				},

	/*
	 * Read with restart
	 */

	[I2C_STATE_R_MORE_TXN_ADDR] = {
				       .entry_fn = go_r_any_txn_addr,
				       .next_state = {
						      [I2C_EVENT_ADDR_SENT_LEN_EQ_1] = I2C_STATE_R_MORE_TXN_PRE_ONE,
						      [I2C_EVENT_ADDR_SENT_LEN_EQ_2] = I2C_STATE_R_MORE_TXN_PRE_FIRST,
						      [I2C_EVENT_ADDR_SENT_LEN_GT_2] = I2C_STATE_R_MORE_TXN_PRE_FIRST,
						      [I2C_EVENT_BUS_ERROR] = I2C_STATE_BUS_ERROR,
						      },
				       },

	[I2C_STATE_R_MORE_TXN_PRE_ONE] = {
					  .entry_fn = go_r_more_txn_pre_one,
					  .next_state = {
							 [I2C_EVENT_TRANSFER_DONE_LEN_EQ_1] = I2C_STATE_R_MORE_TXN_POST_LAST,
							 [I2C_EVENT_BUS_ERROR] = I2C_STATE_BUS_ERROR,
							 },
					  },

	[I2C_STATE_R_MORE_TXN_PRE_FIRST] = {
					    .entry_fn = go_r_any_txn_pre_first,
					    .next_state = {
							   [I2C_EVENT_TRANSFER_DONE_LEN_EQ_2] = I2C_STATE_R_MORE_TXN_PRE_LAST,
							   [I2C_EVENT_TRANSFER_DONE_LEN_GT_2] = I2C_STATE_R_MORE_TXN_PRE_MIDDLE,
							   [I2C_EVENT_BUS_ERROR] = I2C_STATE_BUS_ERROR,
							   },
					    },

	[I2C_STATE_R_MORE_TXN_PRE_MIDDLE] = {
					     .entry_fn = go_r_any_txn_pre_middle,
					     .next_state = {
							    [I2C_EVENT_TRANSFER_DONE_LEN_EQ_2] = I2C_STATE_R_MORE_TXN_PRE_LAST,
							    [I2C_EVENT_TRANSFER_DONE_LEN_GT_2] = I2C_STATE_R_MORE_TXN_PRE_MIDDLE,
							    [I2C_EVENT_BUS_ERROR] = I2C_STATE_BUS_ERROR,
							    },
					     },

	[I2C_STATE_R_MORE_TXN_PRE_LAST] = {
					   .entry_fn = go_r_more_txn_pre_last,
					   .next_state = {
							  [I2C_EVENT_TRANSFER_DONE_LEN_EQ_1] = I2C_STATE_R_MORE_TXN_POST_LAST,
							  [I2C_EVENT_BUS_ERROR] = I2C_STATE_BUS_ERROR,
							  },
					   },

	[I2C_STATE_R_MORE_TXN_POST_LAST] = {
					    .entry_fn = go_r_any_txn_post_last,
					    .next_state = {
							   [I2C_EVENT_AUTO] = I2C_STATE_STARTING,
							   },
					    },

	/*
	 * Read
	 */

	[I2C_STATE_R_LAST_TXN_ADDR] = {
				       .entry_fn = go_r_any_txn_addr,
				       .next_state = {
						      [I2C_EVENT_ADDR_SENT_LEN_EQ_1] = I2C_STATE_R_LAST_TXN_PRE_ONE,
						      [I2C_EVENT_ADDR_SENT_LEN_EQ_2] = I2C_STATE_R_LAST_TXN_PRE_FIRST,
						      [I2C_EVENT_ADDR_SENT_LEN_GT_2] = I2C_STATE_R_LAST_TXN_PRE_FIRST,
						      [I2C_EVENT_BUS_ERROR] = I2C_STATE_BUS_ERROR,
						      },
				       },

	[I2C_STATE_R_LAST_TXN_PRE_ONE] = {
					  .entry_fn = go_r_last_txn_pre_one,
					  .next_state = {
							 [I2C_EVENT_TRANSFER_DONE_LEN_EQ_1] = I2C_STATE_R_LAST_TXN_POST_LAST,
							 [I2C_EVENT_BUS_ERROR] = I2C_STATE_BUS_ERROR,
							 },
					  },

	[I2C_STATE_R_LAST_TXN_PRE_FIRST] = {
					    .entry_fn = go_r_any_txn_pre_first,
					    .next_state = {
							   [I2C_EVENT_TRANSFER_DONE_LEN_EQ_2] = I2C_STATE_R_LAST_TXN_PRE_LAST,
							   [I2C_EVENT_TRANSFER_DONE_LEN_GT_2] = I2C_STATE_R_LAST_TXN_PRE_MIDDLE,
							   [I2C_EVENT_BUS_ERROR] = I2C_STATE_BUS_ERROR,
							   },
					    },

	[I2C_STATE_R_LAST_TXN_PRE_MIDDLE] = {
					     .entry_fn = go_r_any_txn_pre_middle,
					     .next_state = {
							    [I2C_EVENT_TRANSFER_DONE_LEN_EQ_2] = I2C_STATE_R_LAST_TXN_PRE_LAST,
							    [I2C_EVENT_TRANSFER_DONE_LEN_GT_2] = I2C_STATE_R_LAST_TXN_PRE_MIDDLE,
							    [I2C_EVENT_BUS_ERROR] = I2C_STATE_BUS_ERROR,
							    },
					     },

	[I2C_STATE_R_LAST_TXN_PRE_LAST] = {
					   .entry_fn = go_r_last_txn_pre_last,
					   .next_state = {
							  [I2C_EVENT_TRANSFER_DONE_LEN_EQ_1] = I2C_STATE_R_LAST_TXN_POST_LAST,
							  [I2C_EVENT_BUS_ERROR] = I2C_STATE_BUS_ERROR,
							  },
					   },

	[I2C_STATE_R_LAST_TXN_POST_LAST] = {
					    .entry_fn = go_r_any_txn_post_last,
					    .next_state = {
							   [I2C_EVENT_AUTO] = I2C_STATE_STOPPING,
							   },
					    },

	/*
	 * Write with restart
	 */

	[I2C_STATE_W_MORE_TXN_ADDR] = {
				       .entry_fn = go_w_any_txn_addr,
				       .next_state = {
						      [I2C_EVENT_ADDR_SENT_LEN_EQ_1] = I2C_STATE_W_MORE_TXN_LAST,
						      [I2C_EVENT_ADDR_SENT_LEN_EQ_2] = I2C_STATE_W_MORE_TXN_MIDDLE,
						      [I2C_EVENT_ADDR_SENT_LEN_GT_2] = I2C_STATE_W_MORE_TXN_MIDDLE,
					              [I2C_EVENT_NACK] = I2C_STATE_NACK,
						      [I2C_EVENT_BUS_ERROR] = I2C_STATE_BUS_ERROR,
						      },
				       },

	[I2C_STATE_W_MORE_TXN_MIDDLE] = {
					 .entry_fn = go_w_any_txn_middle,
					 .next_state = {
							[I2C_EVENT_TRANSFER_DONE_LEN_EQ_1] = I2C_STATE_W_MORE_TXN_LAST,
							[I2C_EVENT_TRANSFER_DONE_LEN_EQ_2] = I2C_STATE_W_MORE_TXN_MIDDLE,
							[I2C_EVENT_TRANSFER_DONE_LEN_GT_2] = I2C_STATE_W_MORE_TXN_MIDDLE,
						        [I2C_EVENT_NACK] = I2C_STATE_NACK,
							[I2C_EVENT_BUS_ERROR] = I2C_STATE_BUS_ERROR,
							},
					 },

	[I2C_STATE_W_MORE_TXN_LAST] = {
				       .entry_fn = go_w_more_txn_last,
				       .next_state = {
 						      [I2C_EVENT_TRANSFER_DONE_LEN_EQ_0] = I2C_STATE_STARTING,
						      [I2C_EVENT_NACK] = I2C_STATE_NACK,
						      [I2C_EVENT_BUS_ERROR] = I2C_STATE_BUS_ERROR,
						      },
				       },

	/*
	 * Write
	 */

	[I2C_STATE_W_LAST_TXN_ADDR] = {
				       .entry_fn = go_w_any_txn_addr,
				       .next_state = {
						      [I2C_EVENT_ADDR_SENT_LEN_EQ_1] = I2C_STATE_W_LAST_TXN_LAST,
						      [I2C_EVENT_ADDR_SENT_LEN_EQ_2] = I2C_STATE_W_LAST_TXN_MIDDLE,
						      [I2C_EVENT_ADDR_SENT_LEN_GT_2] = I2C_STATE_W_LAST_TXN_MIDDLE,
						      [I2C_EVENT_NACK] = I2C_STATE_NACK,
						      [I2C_EVENT_BUS_ERROR] = I2C_STATE_BUS_ERROR,
						      },
				       },

	[I2C_STATE_W_LAST_TXN_MIDDLE] = {
					 .entry_fn = go_w_any_txn_middle,
					 .next_state = {
							[I2C_EVENT_TRANSFER_DONE_LEN_EQ_1] = I2C_STATE_W_LAST_TXN_LAST,
							[I2C_EVENT_TRANSFER_DONE_LEN_EQ_2] = I2C_STATE_W_LAST_TXN_MIDDLE,
							[I2C_EVENT_TRANSFER_DONE_LEN_GT_2] = I2C_STATE_W_LAST_TXN_MIDDLE,
						        [I2C_EVENT_NACK] = I2C_STATE_NACK,
							[I2C_EVENT_BUS_ERROR] = I2C_STATE_BUS_ERROR,
							},
					 },

	[I2C_STATE_W_LAST_TXN_LAST] = {
				       .entry_fn = go_w_last_txn_last,
				       .next_state = {
						      [I2C_EVENT_TRANSFER_DONE_LEN_EQ_0] = I2C_STATE_STOPPING,
					              [I2C_EVENT_NACK] = I2C_STATE_NACK,
						      [I2C_EVENT_BUS_ERROR] = I2C_STATE_BUS_ERROR,
						      },
				       },
	[I2C_STATE_NACK] = {
		.entry_fn = go_nack,
		.next_state = {
			[I2C_EVENT_AUTO] = I2C_STATE_STOPPING,
		},
	},	
};

static void go_fsm_fault(struct pios_i2c_adapter *i2c_adapter)
{
#if defined(I2C_HALT_ON_ERRORS)
	PIOS_DEBUG_Assert(0);
#endif
	/* Note that this transfer has hit a bus error */
	i2c_adapter->bus_error = true;

	i2c_adapter_reset_bus(i2c_adapter);
	
}

static void go_bus_error(struct pios_i2c_adapter *i2c_adapter)
{
	/* Note that this transfer has hit a bus error */
	i2c_adapter->bus_error = true;

	i2c_adapter_reset_bus(i2c_adapter);
}

static void go_stopping(struct pios_i2c_adapter *i2c_adapter)
{
	I2C_ITConfig(i2c_adapter->cfg->regs, I2C_IT_EVT | I2C_IT_BUF | I2C_IT_ERR, DISABLE);

	/* wake up blocked PIOS_I2C_Transfer() */
	bool woken = false;
	if (PIOS_Semaphore_Give_FromISR(i2c_adapter->sem_ready, &woken) == false) {
#if defined(I2C_HALT_ON_ERRORS)
		PIOS_DEBUG_Assert(0);
#endif
	}
}

static void go_stopped(struct pios_i2c_adapter *i2c_adapter)
{
	I2C_ITConfig(i2c_adapter->cfg->regs, I2C_IT_EVT | I2C_IT_BUF | I2C_IT_ERR, DISABLE);
	I2C_AcknowledgeConfig(i2c_adapter->cfg->regs, ENABLE);
}

static void go_starting(struct pios_i2c_adapter *i2c_adapter)
{
	PIOS_DEBUG_Assert(i2c_adapter->active_txn);
	PIOS_DEBUG_Assert(i2c_adapter->active_txn <= i2c_adapter->last_txn);

	i2c_adapter->active_byte = &(i2c_adapter->active_txn->buf[0]);
	i2c_adapter->last_byte = &(i2c_adapter->active_txn->buf[i2c_adapter->active_txn->len - 1]);

	I2C_GenerateSTART(i2c_adapter->cfg->regs, ENABLE);
	if (i2c_adapter->active_txn->rw == PIOS_I2C_TXN_READ) {
		I2C_ITConfig(i2c_adapter->cfg->regs, I2C_IT_EVT | I2C_IT_BUF | I2C_IT_ERR, ENABLE);
	} else {
		// For write operations, do not enable the IT_BUF events.
		// The current driver does not act when the TX data register is not full, only when the complete byte is sent.
		// With the IT_BUF enabled, we constantly get IRQs, See OP-326
		I2C_ITConfig(i2c_adapter->cfg->regs, I2C_IT_EVT | I2C_IT_ERR, ENABLE);
	}
}

/* Common to 'more' and 'last' transaction */
static void go_r_any_txn_addr(struct pios_i2c_adapter *i2c_adapter)
{
	PIOS_DEBUG_Assert(i2c_adapter->active_txn);
	PIOS_DEBUG_Assert(i2c_adapter->active_txn <= i2c_adapter->last_txn);

	PIOS_DEBUG_Assert(i2c_adapter->active_txn->rw == PIOS_I2C_TXN_READ);

	I2C_Send7bitAddress(i2c_adapter->cfg->regs, (i2c_adapter->active_txn->addr) << 1, I2C_Direction_Receiver);
}

static void go_r_more_txn_pre_one(struct pios_i2c_adapter *i2c_adapter)
{
	I2C_AcknowledgeConfig(i2c_adapter->cfg->regs, DISABLE);
	I2C_GenerateSTART(i2c_adapter->cfg->regs, ENABLE);
}

static void go_r_last_txn_pre_one(struct pios_i2c_adapter *i2c_adapter)
{
	I2C_AcknowledgeConfig(i2c_adapter->cfg->regs, DISABLE);
	I2C_GenerateSTOP(i2c_adapter->cfg->regs, ENABLE);
}

/* Common to 'more' and 'last' transaction */
static void go_r_any_txn_pre_first(struct pios_i2c_adapter *i2c_adapter)
{
	I2C_AcknowledgeConfig(i2c_adapter->cfg->regs, ENABLE);
}

/* Common to 'more' and 'last' transaction */
static void go_r_any_txn_pre_middle(struct pios_i2c_adapter *i2c_adapter)
{
	PIOS_DEBUG_Assert(i2c_adapter->active_byte);
	PIOS_DEBUG_Assert(i2c_adapter->active_byte <= i2c_adapter->last_byte);

	*(i2c_adapter->active_byte) = I2C_ReceiveData(i2c_adapter->cfg->regs);

	/* Move to the next byte */
	i2c_adapter->active_byte++;
	PIOS_DEBUG_Assert(i2c_adapter->active_byte <= i2c_adapter->last_byte);
}

static void go_r_more_txn_pre_last(struct pios_i2c_adapter *i2c_adapter)
{
	PIOS_DEBUG_Assert(i2c_adapter->active_byte);
	PIOS_DEBUG_Assert(i2c_adapter->active_byte <= i2c_adapter->last_byte);

	I2C_AcknowledgeConfig(i2c_adapter->cfg->regs, DISABLE);
	PIOS_IRQ_Disable();
	I2C_ITConfig(i2c_adapter->cfg->regs, I2C_IT_EVT | I2C_IT_BUF | I2C_IT_ERR, DISABLE);
	I2C_GenerateSTART(i2c_adapter->cfg->regs, ENABLE);
	*(i2c_adapter->active_byte) = I2C_ReceiveData(i2c_adapter->cfg->regs);
	I2C_ITConfig(i2c_adapter->cfg->regs, I2C_IT_EVT | I2C_IT_BUF | I2C_IT_ERR, ENABLE);
	PIOS_IRQ_Enable();

	/* Move to the next byte */
	i2c_adapter->active_byte++;
	PIOS_DEBUG_Assert(i2c_adapter->active_byte <= i2c_adapter->last_byte);
}

static void go_r_last_txn_pre_last(struct pios_i2c_adapter *i2c_adapter)
{
	PIOS_DEBUG_Assert(i2c_adapter->active_byte);
	PIOS_DEBUG_Assert(i2c_adapter->active_byte <= i2c_adapter->last_byte);

	I2C_AcknowledgeConfig(i2c_adapter->cfg->regs, DISABLE);
	PIOS_IRQ_Disable();
	I2C_ITConfig(i2c_adapter->cfg->regs, I2C_IT_EVT | I2C_IT_BUF | I2C_IT_ERR, DISABLE);
	I2C_GenerateSTOP(i2c_adapter->cfg->regs, ENABLE);
	*(i2c_adapter->active_byte) = I2C_ReceiveData(i2c_adapter->cfg->regs);
	I2C_ITConfig(i2c_adapter->cfg->regs, I2C_IT_EVT | I2C_IT_BUF | I2C_IT_ERR, ENABLE);
	PIOS_IRQ_Enable();

	/* Move to the next byte */
	i2c_adapter->active_byte++;
	PIOS_DEBUG_Assert(i2c_adapter->active_byte <= i2c_adapter->last_byte);
}

/* Common to 'more' and 'last' transaction */
static void go_r_any_txn_post_last(struct pios_i2c_adapter *i2c_adapter)
{
	PIOS_DEBUG_Assert(i2c_adapter->active_byte);
	PIOS_DEBUG_Assert(i2c_adapter->active_byte == i2c_adapter->last_byte);
	PIOS_DEBUG_Assert(i2c_adapter->active_txn);
	PIOS_DEBUG_Assert(i2c_adapter->active_txn <= i2c_adapter->last_txn);

	*(i2c_adapter->active_byte) = I2C_ReceiveData(i2c_adapter->cfg->regs);

	/* Move to the next byte */
	i2c_adapter->active_byte++;

	/* Move to the next transaction */
	i2c_adapter->active_txn++;
}

/* Common to 'more' and 'last' transaction */
static void go_w_any_txn_addr(struct pios_i2c_adapter *i2c_adapter)
{
	PIOS_DEBUG_Assert(i2c_adapter->active_txn);
	PIOS_DEBUG_Assert(i2c_adapter->active_txn <= i2c_adapter->last_txn);

	PIOS_DEBUG_Assert(i2c_adapter->active_txn->rw == PIOS_I2C_TXN_WRITE);

	I2C_Send7bitAddress(i2c_adapter->cfg->regs, (i2c_adapter->active_txn->addr) << 1, I2C_Direction_Transmitter);
}

static void go_w_any_txn_middle(struct pios_i2c_adapter *i2c_adapter)
{
	PIOS_DEBUG_Assert(i2c_adapter->active_byte);
	PIOS_DEBUG_Assert(i2c_adapter->active_byte < i2c_adapter->last_byte);
	PIOS_DEBUG_Assert(i2c_adapter->active_txn);
	PIOS_DEBUG_Assert(i2c_adapter->active_txn <= i2c_adapter->last_txn);

	I2C_SendData(i2c_adapter->cfg->regs, *(i2c_adapter->active_byte));

	/* Move to the next byte */
	i2c_adapter->active_byte++;
	PIOS_DEBUG_Assert(i2c_adapter->active_byte <= i2c_adapter->last_byte);
}

static void go_w_more_txn_last(struct pios_i2c_adapter *i2c_adapter)
{
	PIOS_DEBUG_Assert(i2c_adapter->active_byte);
	PIOS_DEBUG_Assert(i2c_adapter->active_byte == i2c_adapter->last_byte);
	PIOS_DEBUG_Assert(i2c_adapter->active_txn);
	PIOS_DEBUG_Assert(i2c_adapter->active_txn <= i2c_adapter->last_txn);

	I2C_SendData(i2c_adapter->cfg->regs, *(i2c_adapter->active_byte));

	/* Move to the next byte */
	i2c_adapter->active_byte++;

	/* Move to the next transaction */
	i2c_adapter->active_txn++;
	PIOS_DEBUG_Assert(i2c_adapter->active_txn <= i2c_adapter->last_txn);
}

static void go_w_last_txn_last(struct pios_i2c_adapter *i2c_adapter)
{
	PIOS_DEBUG_Assert(i2c_adapter->active_byte);
	PIOS_DEBUG_Assert(i2c_adapter->active_byte == i2c_adapter->last_byte);
	PIOS_DEBUG_Assert(i2c_adapter->active_txn);
	PIOS_DEBUG_Assert(i2c_adapter->active_txn <= i2c_adapter->last_txn);

	I2C_ITConfig(i2c_adapter->cfg->regs, I2C_IT_BUF, DISABLE);
	I2C_SendData(i2c_adapter->cfg->regs, *(i2c_adapter->active_byte));

// SHOULD MOVE THIS INTO A STOPPING STATE AND SET IT ONLY AFTER THE BYTE WAS SENT
	I2C_GenerateSTOP(i2c_adapter->cfg->regs, ENABLE);

	/* Move to the next byte */
	i2c_adapter->active_byte++;
}

static void go_nack(struct pios_i2c_adapter *i2c_adapter) 
{
	I2C_ITConfig(i2c_adapter->cfg->regs, I2C_IT_EVT | I2C_IT_BUF | I2C_IT_ERR, DISABLE);
	I2C_AcknowledgeConfig(i2c_adapter->cfg->regs, DISABLE);
	I2C_GenerateSTOP(i2c_adapter->cfg->regs, ENABLE);
}

static void i2c_adapter_inject_event(struct pios_i2c_adapter *i2c_adapter, enum i2c_adapter_event event)
{
	PIOS_IRQ_Disable();

#if defined(PIOS_I2C_DIAGNOSTICS)	
	i2c_adapter->i2c_state_event_history[i2c_adapter->i2c_state_event_history_pointer] = event;
	i2c_adapter->i2c_state_event_history_pointer = (i2c_adapter->i2c_state_event_history_pointer + 1) % I2C_LOG_DEPTH;

	i2c_adapter->i2c_state_history[i2c_adapter->i2c_state_history_pointer] = i2c_adapter->curr_state;
	i2c_adapter->i2c_state_history_pointer = (i2c_adapter->i2c_state_history_pointer + 1) % I2C_LOG_DEPTH;
	
	if (i2c_adapter_transitions[i2c_adapter->curr_state].next_state[event] == I2C_STATE_FSM_FAULT)
		i2c_adapter_log_fault(i2c_adapter, PIOS_I2C_ERROR_FSM);
#endif	
	/* 
	 * Move to the next state
	 *
	 * This is done prior to calling the new state's entry function to 
	 * guarantee that the entry function never depends on the previous
	 * state.  This way, it cannot ever know what the previous state was.
	 */
	enum i2c_adapter_state prev_state = i2c_adapter->curr_state;
	if (prev_state) ;

	i2c_adapter->curr_state = i2c_adapter_transitions[i2c_adapter->curr_state].next_state[event];

	/* Call the entry function (if any) for the next state. */
	if (i2c_adapter_transitions[i2c_adapter->curr_state].entry_fn) {
		i2c_adapter_transitions[i2c_adapter->curr_state].entry_fn(i2c_adapter);
	}

	/* Process any AUTO transitions in the FSM */
	i2c_adapter_process_auto(i2c_adapter);

	PIOS_IRQ_Enable();
}

static void i2c_adapter_process_auto(struct pios_i2c_adapter *i2c_adapter)
{
	PIOS_IRQ_Disable();

	enum i2c_adapter_state prev_state = i2c_adapter->curr_state;
	if (prev_state) ;

	while (i2c_adapter_transitions[i2c_adapter->curr_state].next_state[I2C_EVENT_AUTO]) {
		i2c_adapter->curr_state = i2c_adapter_transitions[i2c_adapter->curr_state].next_state[I2C_EVENT_AUTO];

		/* Call the entry function (if any) for the next state. */
		if (i2c_adapter_transitions[i2c_adapter->curr_state].entry_fn) {
			i2c_adapter_transitions[i2c_adapter->curr_state].entry_fn(i2c_adapter);
		}
	}

	PIOS_IRQ_Enable();
}

static void i2c_adapter_fsm_init(struct pios_i2c_adapter *i2c_adapter)
{
	i2c_adapter_reset_bus(i2c_adapter);
	i2c_adapter->curr_state = I2C_STATE_STOPPED;
}

static bool i2c_adapter_wait_for_stopped(struct pios_i2c_adapter *i2c_adapter)
{
	uint32_t guard;

	/*
	 * Wait for the bus to return to the stopped state.
	 * This was pulled out of the FSM due to occasional
	 * failures at this transition which previously resulted
	 * in spinning on this bit in the ISR forever.
	 */
#define I2C_CR1_STOP_REQUESTED 0x0200
	for (guard = 1e6;	/* FIXME: should use the configured bus timeout */
	     guard && (i2c_adapter->cfg->regs->CR1 & I2C_CR1_STOP_REQUESTED); guard--)
		continue;
	if (!guard) {
		/* We timed out waiting for the stop condition */
		return false;
	}

	return true;
}

static void i2c_adapter_reset_bus(struct pios_i2c_adapter *i2c_adapter)
{
        static uint8_t retry_count = 0;
	static uint8_t retry_count_clk = 0;

        /* Reset the I2C block */
	I2C_DeInit(i2c_adapter->cfg->regs);

	/* Make sure the bus is free by clocking it until any slaves release the bus. */
	GPIO_InitTypeDef scl_gpio_init;
	scl_gpio_init = i2c_adapter->cfg->scl.init;
	scl_gpio_init.GPIO_Mode = GPIO_Mode_Out_OD;
	GPIO_SetBits(i2c_adapter->cfg->scl.gpio, i2c_adapter->cfg->scl.init.GPIO_Pin);
	GPIO_Init(i2c_adapter->cfg->scl.gpio, &scl_gpio_init);

	GPIO_InitTypeDef sda_gpio_init;
	sda_gpio_init = i2c_adapter->cfg->sda.init;
	sda_gpio_init.GPIO_Mode = GPIO_Mode_Out_OD;
	GPIO_SetBits(i2c_adapter->cfg->sda.gpio, i2c_adapter->cfg->sda.init.GPIO_Pin);
	GPIO_Init(i2c_adapter->cfg->sda.gpio, &sda_gpio_init);

	/* Check SDA line to determine if slave is asserting bus and clock out if so, this may  */
	/* have to be repeated (due to futher bus errors) but better than clocking 0xFF into an */
	/* ESC */
	//bool sda_hung = GPIO_ReadInputDataBit(i2c_adapter->cfg->sda.gpio, i2c_adapter->cfg->sda.init.GPIO_Pin) == Bit_RESET;
	retry_count_clk = 0;
        while(GPIO_ReadInputDataBit(i2c_adapter->cfg->sda.gpio, i2c_adapter->cfg->sda.init.GPIO_Pin) == Bit_RESET && 
                (retry_count_clk++ < MAX_I2C_RETRY_COUNT)) 
        {
		retry_count = 0;
		/* Set clock high and wait for any clock stretching to finish. */
		GPIO_SetBits(i2c_adapter->cfg->scl.gpio, i2c_adapter->cfg->scl.init.GPIO_Pin);
		while (GPIO_ReadInputDataBit(i2c_adapter->cfg->scl.gpio, i2c_adapter->cfg->scl.init.GPIO_Pin) == Bit_RESET && 
                        retry_count++ < MAX_I2C_RETRY_COUNT)
                    PIOS_DELAY_WaituS(1);
		PIOS_DELAY_WaituS(2);
		
		/* Set clock low */
		GPIO_ResetBits(i2c_adapter->cfg->scl.gpio, i2c_adapter->cfg->scl.init.GPIO_Pin);
		PIOS_DELAY_WaituS(2);
		
		/* Clock high again */
		GPIO_SetBits(i2c_adapter->cfg->scl.gpio, i2c_adapter->cfg->scl.init.GPIO_Pin);
		PIOS_DELAY_WaituS(2);
	}
		
	/* Generate a start then stop condition */
	GPIO_SetBits(i2c_adapter->cfg->scl.gpio, i2c_adapter->cfg->scl.init.GPIO_Pin);
	PIOS_DELAY_WaituS(2);
	GPIO_ResetBits(i2c_adapter->cfg->sda.gpio, i2c_adapter->cfg->sda.init.GPIO_Pin);
	PIOS_DELAY_WaituS(2);
	GPIO_SetBits(i2c_adapter->cfg->sda.gpio, i2c_adapter->cfg->sda.init.GPIO_Pin);
	PIOS_DELAY_WaituS(2);

	/* Set data and clock high and wait for any clock stretching to finish. */
	GPIO_SetBits(i2c_adapter->cfg->sda.gpio, i2c_adapter->cfg->sda.init.GPIO_Pin);
	GPIO_SetBits(i2c_adapter->cfg->scl.gpio, i2c_adapter->cfg->scl.init.GPIO_Pin);

        retry_count = 0;
	while (GPIO_ReadInputDataBit(i2c_adapter->cfg->scl.gpio, i2c_adapter->cfg->scl.init.GPIO_Pin) == Bit_RESET && 
            retry_count++ < MAX_I2C_RETRY_COUNT)
                PIOS_DELAY_WaituS(1);
        /* Wait for data to be high */
        retry_count = 0;
        while (GPIO_ReadInputDataBit(i2c_adapter->cfg->sda.gpio, i2c_adapter->cfg->sda.init.GPIO_Pin) != Bit_SET && 
            retry_count++ < MAX_I2C_RETRY_COUNT)
                PIOS_DELAY_WaituS(1);

	
	/* Bus signals are guaranteed to be high (ie. free) after this point */
	/* Initialize the GPIO pins to the peripheral function */
	GPIO_Init(i2c_adapter->cfg->scl.gpio, (GPIO_InitTypeDef *)&(i2c_adapter->cfg->scl.init));
	GPIO_Init(i2c_adapter->cfg->sda.gpio, (GPIO_InitTypeDef *)&(i2c_adapter->cfg->sda.init));

	/* Reset the I2C block */
	I2C_DeInit(i2c_adapter->cfg->regs);

	/* Initialize the I2C block */
	I2C_Init(i2c_adapter->cfg->regs, (I2C_InitTypeDef *)&(i2c_adapter->cfg->init));

#define I2C_BUSY 0x20
	if (i2c_adapter->cfg->regs->SR2 & I2C_BUSY) {
		/* Reset the I2C block */
		I2C_SoftwareResetCmd(i2c_adapter->cfg->regs, ENABLE);
		I2C_SoftwareResetCmd(i2c_adapter->cfg->regs, DISABLE);
	}
}

/* Return true if the FSM is in a terminal state */
static bool i2c_adapter_fsm_terminated(struct pios_i2c_adapter *i2c_adapter)
{
	switch (i2c_adapter->curr_state) {
	case I2C_STATE_STOPPING:
	case I2C_STATE_STOPPED:
		return (true);
	default:
		return (false);
	}
}

/**
 * Logs the last N state transitions and N IRQ events due to
 * an error condition
 * \param[in] i2c the adapter number to log an event for
 */
#if defined(PIOS_I2C_DIAGNOSTICS)
void i2c_adapter_log_fault(struct pios_i2c_adapter *i2c_adapter, enum pios_i2c_error_type type)
{
	i2c_adapter->i2c_adapter_fault_history.type = type;
	for (uint8_t i = 0; i < I2C_LOG_DEPTH; i++) {
		i2c_adapter->i2c_adapter_fault_history.evirq[i] =
				i2c_adapter->i2c_evirq_history[(I2C_LOG_DEPTH + i2c_adapter->i2c_evirq_history_pointer - 1 - i) % I2C_LOG_DEPTH];
		i2c_adapter->i2c_adapter_fault_history.erirq[i] =
				i2c_adapter->i2c_erirq_history[(I2C_LOG_DEPTH + i2c_adapter->i2c_erirq_history_pointer - 1 - i) % I2C_LOG_DEPTH];
		i2c_adapter->i2c_adapter_fault_history.event[i] =
				i2c_adapter->i2c_state_event_history[(I2C_LOG_DEPTH + i2c_adapter->i2c_state_event_history_pointer - 1 - i) % I2C_LOG_DEPTH];
		i2c_adapter->i2c_adapter_fault_history.state[i] =
				i2c_adapter->i2c_state_history[(I2C_LOG_DEPTH + i2c_adapter->i2c_state_history_pointer - 1 - i) % I2C_LOG_DEPTH];
	}
	switch(type) {
		case PIOS_I2C_ERROR_EVENT:
			i2c_adapter->i2c_bad_event_counter++;
			break;
		case PIOS_I2C_ERROR_FSM:
			i2c_adapter->i2c_fsm_fault_count++;
			break;
		case PIOS_I2C_ERROR_INTERRUPT:
			i2c_adapter->i2c_error_interrupt_counter++;
			break;
	}
}
#endif

static bool PIOS_I2C_validate(struct pios_i2c_adapter * i2c_adapter)
{
	return (i2c_adapter->magic == PIOS_I2C_DEV_MAGIC);
}

static struct pios_i2c_adapter * PIOS_I2C_alloc(void)
{
	struct pios_i2c_adapter * i2c_adapter;

	i2c_adapter = (struct pios_i2c_adapter *)PIOS_malloc(sizeof(*i2c_adapter));
	if (!i2c_adapter) return(NULL);

	i2c_adapter->magic = PIOS_I2C_DEV_MAGIC;
	return(i2c_adapter);
}

/**
* Initializes IIC driver
* \param[in] mode currently only mode 0 supported
* \return < 0 if initialisation failed
*/
int32_t PIOS_I2C_Init(uint32_t * i2c_id, const struct pios_i2c_adapter_cfg * cfg)
{
	PIOS_DEBUG_Assert(i2c_id);
	PIOS_DEBUG_Assert(cfg);

	struct pios_i2c_adapter * i2c_adapter;

	i2c_adapter = (struct pios_i2c_adapter *) PIOS_I2C_alloc();
	if (!i2c_adapter) goto out_fail;

	/* Bind the configuration to the device instance */
	i2c_adapter->cfg = cfg;

	i2c_adapter->sem_ready = PIOS_Semaphore_Create();
	i2c_adapter->lock = PIOS_Mutex_Create();

	/* Enable the associated peripheral clock */
	switch ((uint32_t) i2c_adapter->cfg->regs) {
	case (uint32_t) I2C1:
		/* Enable I2C peripheral clock (APB1 == slow speed) */
		RCC_APB1PeriphClockCmd(RCC_APB1Periph_I2C1, ENABLE);
		break;
	case (uint32_t) I2C2:
		/* Enable I2C peripheral clock (APB1 == slow speed) */
		RCC_APB1PeriphClockCmd(RCC_APB1Periph_I2C2, ENABLE);
		break;
	}

	if (i2c_adapter->cfg->remap) {
		GPIO_PinRemapConfig(i2c_adapter->cfg->remap, ENABLE);
	}

	/* Initialize the state machine */
	i2c_adapter_fsm_init(i2c_adapter);

	*i2c_id = (uint32_t)i2c_adapter;

	/* Configure and enable I2C interrupts */
	NVIC_Init((NVIC_InitTypeDef *)&(i2c_adapter->cfg->event.init));
	NVIC_Init((NVIC_InitTypeDef *)&(i2c_adapter->cfg->error.init));
	
	/* No error */
	return 0;

out_fail:
	return(-1);
}

/**
 * @brief Check the I2C bus is clear and in a properly reset state
 * @returns  0 Bus is clear 
 * @returns -1 Bus is in use
 * @returns -2 Bus not clear
 */
int32_t PIOS_I2C_CheckClear(uint32_t i2c_id)
{
	struct pios_i2c_adapter * i2c_adapter = (struct pios_i2c_adapter *)i2c_id;

	bool valid = PIOS_I2C_validate(i2c_adapter);
	PIOS_Assert(valid)

	if (PIOS_Mutex_Lock(i2c_adapter->lock, 0) == false)
		return -1;

	if (i2c_adapter->curr_state != I2C_STATE_STOPPED) {
		PIOS_Mutex_Unlock(i2c_adapter->lock);
		return -2;
	}

	if (GPIO_ReadInputDataBit(i2c_adapter->cfg->sda.gpio, i2c_adapter->cfg->sda.init.GPIO_Pin) == Bit_RESET ||
		GPIO_ReadInputDataBit(i2c_adapter->cfg->scl.gpio, i2c_adapter->cfg->scl.init.GPIO_Pin) == Bit_RESET) {
		PIOS_Mutex_Unlock(i2c_adapter->lock);
		return -3;
	}

	PIOS_Mutex_Unlock(i2c_adapter->lock);

	return 0;
}

/**
 * @brief Perform a series of I2C transactions
 * @returns 0 if success or error code
 * @retval -1 for failed transaction 
 * @retval -2 for failure to get semaphore
 */
int32_t PIOS_I2C_Transfer(uint32_t i2c_id, const struct pios_i2c_txn txn_list[], uint32_t num_txns)
{
	struct pios_i2c_adapter * i2c_adapter = (struct pios_i2c_adapter *)i2c_id;

	bool valid = PIOS_I2C_validate(i2c_adapter);
	PIOS_Assert(valid)

	PIOS_DEBUG_Assert(txn_list);
	PIOS_DEBUG_Assert(num_txns);

	if (PIOS_Mutex_Lock(i2c_adapter->lock, i2c_adapter->cfg->transfer_timeout_ms) == false)
		return -2;

	PIOS_DEBUG_Assert(i2c_adapter->curr_state == I2C_STATE_STOPPED);

	i2c_adapter->last_txn = &txn_list[num_txns - 1];
	i2c_adapter->active_txn = &txn_list[0];

	/* Make sure the done/ready semaphore is consumed before we start */
	PIOS_Semaphore_Take(i2c_adapter->sem_ready, 0);

	i2c_adapter->bus_error = false;
	i2c_adapter_inject_event(i2c_adapter, I2C_EVENT_START);

	/* Wait for the transfer to complete */
	bool semaphore_success = (PIOS_Semaphore_Take(i2c_adapter->sem_ready, i2c_adapter->cfg->transfer_timeout_ms) == true);

	/* Spin waiting for the transfer to finish */
	while (!i2c_adapter_fsm_terminated(i2c_adapter)) ;

	if (i2c_adapter_wait_for_stopped(i2c_adapter)) {
		i2c_adapter_inject_event(i2c_adapter, I2C_EVENT_STOPPED);
	} else {
		i2c_adapter_fsm_init(i2c_adapter);
	}

#if defined(PIOS_I2C_DIAGNOSTICS)
	if (!semaphore_success)
		i2c_adapter->i2c_timeout_counter++;
#endif

	int32_t result = !semaphore_success ? -2 :
			i2c_adapter->bus_error ? -1 :
			0;

	PIOS_Mutex_Unlock(i2c_adapter->lock);

	return result;
}


void PIOS_I2C_EV_IRQ_Handler(uint32_t i2c_id)
{
	struct pios_i2c_adapter * i2c_adapter = (struct pios_i2c_adapter *)i2c_id;

	bool valid = PIOS_I2C_validate(i2c_adapter);
	PIOS_Assert(valid)

	uint32_t event = I2C_GetLastEvent(i2c_adapter->cfg->regs);

#if defined(PIOS_I2C_DIAGNOSTICS)	
	/* Store event for diagnostics */
	i2c_adapter->i2c_evirq_history[i2c_adapter->i2c_evirq_history_pointer] = event;
	i2c_adapter->i2c_evirq_history_pointer = (i2c_adapter->i2c_evirq_history_pointer + 1) % I2C_LOG_DEPTH;
#endif
	
#define EVENT_MASK 0x000700FF
	event &= EVENT_MASK;
	
	
	switch (event) { /* Mask out all the bits we don't care about */
	case (I2C_EVENT_MASTER_MODE_SELECT | 0x40):
		/* Unexplained event: EV5 + RxNE : Extraneous Rx.  Probably a late NACK from previous read. */
		/* Clean up the extra Rx until the root cause is identified and just keep going */
		(void)I2C_ReceiveData(i2c_adapter->cfg->regs);
		/* Fall through */
	case I2C_EVENT_MASTER_MODE_SELECT:	/* EV5 */
		switch (i2c_adapter->active_txn->rw) {
		case PIOS_I2C_TXN_READ:
			if (i2c_adapter->active_txn == i2c_adapter->last_txn) {
				/* Final transaction */
				i2c_adapter_inject_event(i2c_adapter, I2C_EVENT_STARTED_LAST_TXN_READ);
			} else if (i2c_adapter->active_txn < i2c_adapter->last_txn) {
				/* More transactions follow */
				i2c_adapter_inject_event(i2c_adapter, I2C_EVENT_STARTED_MORE_TXN_READ);
			} else {
				PIOS_DEBUG_Assert(0);
			}
			break;
		case PIOS_I2C_TXN_WRITE:
			if (i2c_adapter->active_txn == i2c_adapter->last_txn) {
				/* Final transaction */
				i2c_adapter_inject_event(i2c_adapter, I2C_EVENT_STARTED_LAST_TXN_WRITE);
			} else if (i2c_adapter->active_txn < i2c_adapter->last_txn) {
				/* More transactions follow */
				i2c_adapter_inject_event(i2c_adapter, I2C_EVENT_STARTED_MORE_TXN_WRITE);
			} else {
				PIOS_DEBUG_Assert(0);
			}
			break;
		default:
			PIOS_DEBUG_Assert(0);
			break;
		}
		break;
	case I2C_EVENT_MASTER_TRANSMITTER_MODE_SELECTED:	/* EV6 */
	case I2C_EVENT_MASTER_RECEIVER_MODE_SELECTED:	/* EV6 */
		switch (i2c_adapter->last_byte - i2c_adapter->active_byte + 1) {
		case 0:
			i2c_adapter_inject_event(i2c_adapter, I2C_EVENT_ADDR_SENT_LEN_EQ_0);
			break;
		case 1:
			i2c_adapter_inject_event(i2c_adapter, I2C_EVENT_ADDR_SENT_LEN_EQ_1);
			break;
		case 2:
			i2c_adapter_inject_event(i2c_adapter, I2C_EVENT_ADDR_SENT_LEN_EQ_2);
			break;
		default:
			i2c_adapter_inject_event(i2c_adapter, I2C_EVENT_ADDR_SENT_LEN_GT_2);
			break;
		}
		break;
	case 0x80:		/* TxE only.  TRA + MSL + BUSY have been cleared before we got here. */
		/* Ignore */
		{
			static volatile bool halt = false;
			while (halt) ;
		}
		break;
	case 0:                 /* This triggers an FSM fault sometimes, but not having it stops things working */
	case 0x40:		/* RxNE only.  MSL + BUSY have already been cleared by HW. */
	case 0x44:		/* RxNE + BTF.  MSL + BUSY have already been cleared by HW. */
	case I2C_EVENT_MASTER_BYTE_RECEIVED:	/* EV7 */
	case (I2C_EVENT_MASTER_BYTE_RECEIVED | 0x4):	/* EV7 + BTF */
	case I2C_EVENT_MASTER_BYTE_TRANSMITTED:	/* EV8_2 */
	case 0x84:		/* TxE + BTF. EV8_2 but TRA + MSL + BUSY have already been cleared by HW. */
		switch (i2c_adapter->last_byte - i2c_adapter->active_byte + 1) {
		case 0:
			i2c_adapter_inject_event(i2c_adapter, I2C_EVENT_TRANSFER_DONE_LEN_EQ_0);
			break;
		case 1:
			i2c_adapter_inject_event(i2c_adapter, I2C_EVENT_TRANSFER_DONE_LEN_EQ_1);
			break;
		case 2:
			i2c_adapter_inject_event(i2c_adapter, I2C_EVENT_TRANSFER_DONE_LEN_EQ_2);
			break;
		default:
			i2c_adapter_inject_event(i2c_adapter, I2C_EVENT_TRANSFER_DONE_LEN_GT_2);
			break;
		}
		break;
	case I2C_EVENT_MASTER_BYTE_TRANSMITTING:	/* EV8 */
		/* Ignore this event and wait for TRANSMITTED in case we can't keep up */
		goto skip_event;
		break;
	case 0x30084: /* Occurs between byte tranmistted and master mode selected */
	case 0x30000: /* Need to throw away this spurious event */
	case 0x30403 & EVENT_MASK: /* Detected this after got a NACK, probably stop bit */			
		goto skip_event;
		break; 
	default:
#if defined(PIOS_I2C_DIAGNOSTICS)
		i2c_adapter_log_fault(i2c_adapter, PIOS_I2C_ERROR_EVENT);
#endif
#if defined(I2C_HALT_ON_ERRORS)
		PIOS_DEBUG_Assert(0);
#endif
		i2c_adapter_inject_event(i2c_adapter, I2C_EVENT_BUS_ERROR);
		break;
	}

skip_event:
	;
}


void PIOS_I2C_ER_IRQ_Handler(uint32_t i2c_id)
{
	struct pios_i2c_adapter * i2c_adapter = (struct pios_i2c_adapter *)i2c_id;

	bool valid = PIOS_I2C_validate(i2c_adapter);
	PIOS_Assert(valid)

	uint32_t event = I2C_GetLastEvent(i2c_adapter->cfg->regs);

#if defined(PIOS_I2C_DIAGNOSTICS)
	i2c_adapter->i2c_erirq_history[i2c_adapter->i2c_erirq_history_pointer] = event;
	i2c_adapter->i2c_erirq_history_pointer = (i2c_adapter->i2c_erirq_history_pointer + 1) % I2C_LOG_DEPTH;
#endif

	if(event & I2C_FLAG_AF) {
#if defined(PIOS_I2C_DIAGNOSTICS)
		i2c_adapter->i2c_nack_counter++;
#endif

		I2C_ClearFlag(i2c_adapter->cfg->regs, I2C_FLAG_AF);

		i2c_adapter_inject_event(i2c_adapter, I2C_EVENT_NACK);
	} else { /* Mostly bus errors here */
#if defined(PIOS_I2C_DIAGNOSTICS)
		i2c_adapter_log_fault(i2c_adapter, PIOS_I2C_ERROR_INTERRUPT);
#endif
		/* Fail hard on any errors for now */
		i2c_adapter_inject_event(i2c_adapter, I2C_EVENT_BUS_ERROR);
	}	
}

#endif

/**
  * @}
  * @}
  */
