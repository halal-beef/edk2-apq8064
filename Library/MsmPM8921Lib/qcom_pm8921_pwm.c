/*
 * * Copyright (c) 2011, Code Aurora Forum. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *  * Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above
 *    copyright notice, this list of conditions and the following
 *    disclaimer in the documentation and/or other materials provided
 *    with the distribution.
 *  * Neither the name of Code Aurora Forum, Inc. nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <Library/IoLib.h>
#include <Library/DebugLib.h>

#include <Library/qcom_pm8921.h>
#include <Library/qcom_pm8921_pwm.h>


char *clks[NUM_CLOCKS] = 
{
	"1K", "32768", "19.2M"
};

unsigned pre_div[NUM_PRE_DIVIDE] = 
{
	PRE_DIVIDE_0, 
	PRE_DIVIDE_1, 
	PRE_DIVIDE_2, 
	PRE_DIVIDE_3
};

unsigned int pt_t[NUM_PRE_DIVIDE][NUM_CLOCKS] = 
{
	{
		PRE_DIVIDE_0 * NSEC_1000HZ,
		PRE_DIVIDE_0 * NSEC_32768HZ,
		PRE_DIVIDE_0 * NSEC_19P2MHZ,
	},
	{	PRE_DIVIDE_1 * NSEC_1000HZ,
		PRE_DIVIDE_1 * NSEC_32768HZ,
		PRE_DIVIDE_1 * NSEC_19P2MHZ,
	},
	{	PRE_DIVIDE_2 * NSEC_1000HZ,
		PRE_DIVIDE_2 * NSEC_32768HZ,
		PRE_DIVIDE_2 * NSEC_19P2MHZ,
	},
	{	PRE_DIVIDE_2 * NSEC_1000HZ,
		PRE_DIVIDE_2 * NSEC_32768HZ,
		PRE_DIVIDE_2 * NSEC_19P2MHZ,
	},
};

UINT16 duty_msec[PM_PWM_1KHZ_COUNT_MAX + 1] = 
{
	0, 1, 2, 3, 4, 6, 8, 16, 18, 24, 32, 36, 64, 128, 256, 512
};

UINT16 pause_count[PM_PWM_PAUSE_COUNT_MAX + 1] = 
{
	1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16,
	23, 28, 31, 42, 47, 56, 63, 83, 94, 111, 125, 167, 188, 222, 250, 333,
	375, 500, 667, 750, 800, 900, 1000, 1100,
	1200, 1300, 1400, 1500, 1600, 1800, 2000, 2500,
	3000, 3500, 4000, 4500, 5000, 5500, 6000, 6500,
	7000
};

/* Function to get the PWM size, divider, clock for the given period */

void pm8921_pwm_calc_period(UINT32 period_us,struct pm8921_pwm_config *pwm_conf)
{
	int n, m, clk, div;
	int best_m, best_div, best_clk;
	int last_err, cur_err, better_err, better_m;
	UINT32 tmp_p, last_p, min_err, period_n;

	/* PWM Period / N : handle underflow or overflow */
	if (period_us < (PM_PWM_PERIOD_MAX / NSEC_PER_USEC))
		period_n = (period_us * NSEC_PER_USEC) >> 6;
	else
		period_n = (period_us >> 6) * NSEC_PER_USEC;

	if (period_n >= MAX_MPT)
	{
		n = 9;
		period_n >>= 3;
	}
	else
		n = 6;

	min_err = MAX_MPT;
	best_m = 0;
	best_clk = 0;
	best_div = 0;

	for (clk = 0; clk < NUM_CLOCKS; clk++)
	{
		for (div = 0; div < NUM_PRE_DIVIDE; div++)
		{
			tmp_p = period_n;
			last_p = tmp_p;
			for (m = 0; m <= PM_PWM_M_MAX; m++)
			{
				if (tmp_p <= pt_t[div][clk])
				{
					/* Found local best */
					if (!m)
					{
						better_err = pt_t[div][clk] - tmp_p;
						better_m = m;
					}
					else
					{
						last_err = last_p - pt_t[div][clk];
						cur_err = pt_t[div][clk] - tmp_p;

						if (cur_err < last_err)
						{
							better_err = cur_err;
							better_m = m;
						}
						else
						{
							better_err = last_err;
							better_m = m - 1;
						}
					}

					if (better_err < min_err)
					{
						min_err = better_err;
						best_m = better_m;
						best_clk = clk;
						best_div = div;
					}
					break;
				}
				else
				{
					last_p = tmp_p;
					tmp_p >>= 1;
				}
			}
		}
	}

	pwm_conf->pwm_size = n;
	pwm_conf->clk = best_clk;
	pwm_conf->pre_div = best_div;
	pwm_conf->pre_div_exp = best_m;
}

/* Function to configure PWM control registers with clock, divider values */

int pm8921_pwm_configure(UINT8 pwm_id,struct pm8921_pwm_config *pwm_conf,pm8921_dev_t *dev)
{
	int i, len, rc = -1;
	UINT8 reg;

	reg = (pwm_conf->pwm_size > 6) ? PM_PWM_SIZE_9_BIT : 0;
	pwm_conf->pwm_ctl[5] = reg;

	reg = ((pwm_conf->clk + 1) << PM_PWM_CLK_SEL_SHIFT)
		& PM_PWM_CLK_SEL_MASK;
	reg |= (pwm_conf->pre_div << PM_PWM_PREDIVIDE_SHIFT)
		& PM_PWM_PREDIVIDE_MASK;
	reg |= pwm_conf->pre_div_exp & PM_PWM_M_MASK;
	pwm_conf->pwm_ctl[4] = reg;

	/* Just to let know we bypass LUT */
	if (pwm_conf->bypass_lut)
	{
		/* CTL0 is set in pwm_enable() */
		pwm_conf->pwm_ctl[0] &= PM_PWM_PWM_START;
		pwm_conf->pwm_ctl[1] = PM_PWM_BYPASS_LUT;
		pwm_conf->pwm_ctl[2] = 0;

		if (pwm_conf->pwm_size > 6)
		{
			pwm_conf->pwm_ctl[3] = pwm_conf->pwm_value
						& PM_PWM_VALUE_BIT7_0;
			pwm_conf->pwm_ctl[4] |= (pwm_conf->pwm_value >> 1)
						& PM_PWM_VALUE_BIT8;
		}
		else
		{
			pwm_conf->pwm_ctl[3] = pwm_conf->pwm_value
						& PM_PWM_VALUE_BIT5_0;
		}

		len = 6;
	}
	else
	{
		/* Right now, we are not using LUT */
		goto bail_out;
	}

	/* Selecting the bank */
	rc = dev->write(&pwm_id, 1, PM8921_LPG_BANK_SEL);
	if (rc)
		goto bail_out;

	for (i = 0; i < len; i++)
	{
		rc = dev->write(&pwm_conf->pwm_ctl[i], 1, PM8921_LPG_CTL(i));
		if (rc)
		{
			DEBUG((EFI_D_WARN, "pm8921_write() failed in pwm_configure %d\n", rc));
			break;
		}
	}

bail_out:
	if (rc)
		DEBUG((EFI_D_WARN, "Error in pm8921_pwm_configure()\n"));
	return rc;
}

/* Top level function for configuring PWM
 * Always called from the main pm8921.c file
 */

int pm8921_pwm_config(UINT8 pwm_id,UINT32 duty_us,UINT32 period_us,pm8921_dev_t *dev)
{
	struct pm8921_pwm_config pwm_conf;
	UINT32 max_pwm_value, tmp;
	int rc = -1;

	if ((duty_us > period_us) || (period_us > PM_PWM_PERIOD_MAX) || (period_us < PM_PWM_PERIOD_MIN))
	{
		DEBUG((EFI_D_WARN, "Error in duty cycle and period\n"));
		return -1;
	}

	pm8921_pwm_calc_period(period_us, &pwm_conf);

	/* Figure out pwm_value with overflow handling */
	if (period_us > (1 << pwm_conf.pwm_size))
	{
		tmp = period_us;
		tmp >>= pwm_conf.pwm_size;
		pwm_conf.pwm_value = duty_us / tmp;
	}
	else
	{
		tmp = duty_us;
		tmp <<= pwm_conf.pwm_size;
		pwm_conf.pwm_value = tmp / period_us;
	}

	max_pwm_value = (1 << pwm_conf.pwm_size) - 1;

	if (pwm_conf.pwm_value > max_pwm_value)
		pwm_conf.pwm_value = max_pwm_value;

	/* Bypassing LUT */
	pwm_conf.bypass_lut = 1;

	DEBUG((EFI_D_WARN, "duty/period=%u/%u usec: pwm_value=%d (of %d)\n", duty_us, period_us, pwm_conf.pwm_value, 1 << pwm_conf.pwm_size));

	rc = pm8921_pwm_configure(pwm_id, &pwm_conf, dev);

	if (rc)
		DEBUG((EFI_D_WARN, "Error in pwm_config()\n"));

	return rc;
}

/* Top level function to enable PWM with specified id
 * Always called from the main pm8921.c file
 */


int pm8921_pwm_enable(UINT8 pwm_id, pm8921_dev_t *dev)
{
	int rc = -1;
	UINT8 reg;

	/* Read it before enabling other bank */
	rc = dev->read(&reg, 1, PM8921_LPG_BANK_ENABLE);
	if (rc)
		goto bail_out;

	reg |= (1 << pwm_id);

	rc = dev->write(&reg, 1, PM8921_LPG_BANK_ENABLE);
	if (rc)
		goto bail_out;

	/* Selecting the bank */
	rc = dev->write(&pwm_id, 1, PM8921_LPG_BANK_SEL);
	if (rc)
		goto bail_out;

	/* Read it before setting PWM start */
	rc = dev->read(&reg, 1, PM8921_LPG_CTL(0));
	if (rc)
		goto bail_out;

	reg |= PM_PWM_PWM_START;
	reg &= ~PM_PWM_RAMP_GEN_START;
	rc = dev->write(&reg, 1, PM8921_LPG_CTL(0));

bail_out:
	if (rc)
		DEBUG((EFI_D_WARN, "Error in pwm_enable()\n"));
	return rc;
}
