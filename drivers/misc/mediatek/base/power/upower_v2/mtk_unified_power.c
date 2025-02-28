/*
 * Copyright (C) 2016 MediaTek Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See http://www.gnu.org/licenses/gpl-2.0.html for more details.
 */

#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/export.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/ktime.h>
#include <linux/math64.h>
#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/rcupdate.h>
#include <linux/sched.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/types.h>
#include <mt-plat/mtk_chip.h>

/* local include */
#include "mtk_upower.h"

#ifndef EARLY_PORTING_EEM
#include "mtk_eem.h"
#endif

#if UPOWER_ENABLE_TINYSYS_SSPM
#include <sspm_reservedmem_define.h>
#endif

#ifndef EARLY_PORTING_SPOWER
#include "mtk_common_static_power.h"
#endif

#ifdef UPOWER_USE_QOS_IPI
#if UPOWER_ENABLE_TINYSYS_SSPM
#include <mtk_spm_vcore_dvfs_ipi.h>
#include <mtk_vcorefs_governor.h>
#if defined(CONFIG_MACH_MT6768)
#include <helio-dvfsrc-ipi.h>
#endif
#endif
#endif

#if UPOWER_ENABLE
unsigned char upower_enable = 1;
#else
unsigned char upower_enable;
#endif
#define LL_CORE_NUM 4
#define L_CORE_NUM 2
#define LKG_IDX 0

/* reference to target upower tbl, ex: big upower tbl */
struct upower_tbl *upower_tbl_ref;
/* Used for rcu lock, points to all the target tables list*/
struct upower_tbl_info *new_p_tbl_infos;
/* reference to raw upower tbl lists */
struct upower_tbl_info *upower_tbl_infos;
/* sspm reserved mem info for sspm upower */
phys_addr_t upower_data_phy_addr, upower_data_virt_addr;
unsigned long long upower_data_size;

#if 0
static void print_tbl(void)
{
	int i, j;
/* --------------------print static orig table -------------------------*/
#if 0
	struct upower_tbl *tbl;

	for (i = 0; i < NR_UPOWER_BANK; i++) {
		tbl = upower_tbl_infos[i].p_upower_tbl;
		/* table size must be 512 bytes */
		upower_debug("Bank %d , tbl size %ld\n",
				i, sizeof(struct upower_tbl));
		for (j = 0; j < UPOWER_OPP_NUM; j++) {
			upower_debug
		(" cap, volt, dyn, lkg: %llu, %u, %u, {%u, %u, %u, %u, %u}\n",
				tbl->row[j].cap, tbl->row[j].volt,
				tbl->row[j].dyn_pwr, tbl->row[j].lkg_pwr[0],
				tbl->row[j].lkg_pwr[1],
				tbl->row[j].lkg_pwr[2],
				tbl->row[j].lkg_pwr[3],
				tbl->row[j].lkg_pwr[4]);
		}

		upower_debug(" lkg_idx, num_row: %d, %d\n",
				tbl->lkg_idx, tbl->row_num);
		upower_debug("---------------------------------------------\n");
	}
#else
/* --------------------print sram table -------------------------*/
	for (i = 0; i < NR_UPOWER_BANK; i++) {
		/* table size must be 512 bytes */
		upower_debug("---Bank %d , tbl size %ld---\n",
			i, sizeof(struct upower_tbl));
		for (j = 0; j < UPOWER_OPP_NUM; j++) {
			upower_debug
	(" cap = %llu, volt = %u, dyn = %u, lkg = {%u, %u, %u, %u, %u}\n",
			upower_tbl_ref[i].row[j].cap,
			upower_tbl_ref[i].row[j].volt,
			upower_tbl_ref[i].row[j].dyn_pwr,
			upower_tbl_ref[i].row[j].lkg_pwr[0],
			upower_tbl_ref[i].row[j].lkg_pwr[1],
			upower_tbl_ref[i].row[j].lkg_pwr[2],
			upower_tbl_ref[i].row[j].lkg_pwr[3],
			upower_tbl_ref[i].row[j].lkg_pwr[4]);
		}
		upower_debug(" lkg_idx, num_row: %d, %d\n",
			upower_tbl_ref[i].lkg_idx, upower_tbl_ref[i].row_num);
		upower_debug("-------------------------------------------------\n");
	}
#endif
}
#endif

#ifdef UPOWER_UT
void upower_ut(void)
{
	struct upower_tbl_info **addr_ptr_tbl_info;
	struct upower_tbl_info *ptr_tbl_info;
	struct upower_tbl *ptr_tbl;
	int i, j;

	upower_debug("----upower_get_tbl()----\n");
	/* get addr of ptr which points to upower_tbl_infos[] */
	addr_ptr_tbl_info = upower_get_tbl();
	/* get ptr which points to upower_tbl_infos[] */
	ptr_tbl_info = *addr_ptr_tbl_info;
	upower_debug("get upower tbl location = %p\n",
		     ptr_tbl_info[0].p_upower_tbl);
#if 0
	upower_debug("ptr_tbl_info --> %p --> tbl %p (p_upower_tbl_infos --> %p)\n",
		ptr_tbl_info, ptr_tbl_info[0].p_upower_tbl, p_upower_tbl_infos);
#endif

	/* print all the tables that record in upower_tbl_infos[]*/
	for (i = 0; i < NR_UPOWER_BANK; i++) {
		upower_debug("bank %d\n", i);
		ptr_tbl = ptr_tbl_info[i].p_upower_tbl;
		for (j = 0; j < UPOWER_OPP_NUM; j++) {
			upower_debug(
				" cap = %llu, volt = %u, dyn = %u, lkg = {%u, %u, %u, %u, %u, %u}\n",
				ptr_tbl->row[j].cap, ptr_tbl->row[j].volt,
				ptr_tbl->row[j].dyn_pwr,
				ptr_tbl->row[j].lkg_pwr[0],
				ptr_tbl->row[j].lkg_pwr[1],
				ptr_tbl->row[j].lkg_pwr[2],
				ptr_tbl->row[j].lkg_pwr[3],
				ptr_tbl->row[j].lkg_pwr[4],
				ptr_tbl->row[j].lkg_pwr[5]);
		}
		upower_debug(" lkg_idx, num_row, nr_idle_states: %d, %d ,%d\n",
			     ptr_tbl->lkg_idx, ptr_tbl->row_num,
			     ptr_tbl->nr_idle_states);

		for (i = 0; i < NR_UPOWER_DEGREE; i++) {
			upower_debug("(%d)C c0 = %lu, c1 = %lu\n",
				     degree_set[i],
				     ptr_tbl->idle_states[i][0].power,
				     ptr_tbl->idle_states[i][1].power);
		}
	}

	upower_debug("@@turn_point= %d\n", upower_get_turn_point());
	upower_debug("----upower_get_power()----\n");
	for (i = 0; i < NR_UPOWER_BANK; i++) {
		upower_debug("bank %d\n", i);
		upower_debug("[dyn] %u, %u, %u, %u, %u, %u, %u, %u, %u\n",
			     upower_get_power(i, 0, UPOWER_DYN),
			     upower_get_power(i, 1, UPOWER_DYN),
			     upower_get_power(i, 2, UPOWER_DYN),
			     upower_get_power(i, 3, UPOWER_DYN),
			     upower_get_power(i, 4, UPOWER_DYN),
			     upower_get_power(i, 5, UPOWER_DYN),
			     upower_get_power(i, 6, UPOWER_DYN),
			     upower_get_power(i, 7, UPOWER_DYN),
			     upower_get_power(i, 15, UPOWER_DYN));
		upower_debug("[lkg] %u, %u, %u, %u, %u, %u, %u, %u, %u\n",
			     upower_get_power(i, 0, UPOWER_LKG),
			     upower_get_power(i, 1, UPOWER_LKG),
			     upower_get_power(i, 2, UPOWER_LKG),
			     upower_get_power(i, 3, UPOWER_LKG),
			     upower_get_power(i, 4, UPOWER_LKG),
			     upower_get_power(i, 5, UPOWER_LKG),
			     upower_get_power(i, 6, UPOWER_LKG),
			     upower_get_power(i, 7, UPOWER_LKG),
			     upower_get_power(i, 15, UPOWER_LKG));
	}
}
#endif

static void upower_update_dyn_pwr(void)
{
	unsigned long long refPower, newVolt, refVolt, newPower;
	unsigned long long temp1, temp2;
	int i, j;
	struct upower_tbl *tbl;

	for (i = 0; i < NR_UPOWER_BANK; i++) {
		tbl = upower_tbl_infos[i].p_upower_tbl;
		for (j = 0; j < UPOWER_OPP_NUM; j++) {
			refPower = (unsigned long long)tbl->row[j].dyn_pwr;
			refVolt = (unsigned long long)tbl->row[j].volt;
			newVolt = (unsigned long long)upower_tbl_ref[i]
					  .row[j]
					  .volt;

			temp1 = (refPower * newVolt * newVolt);
			temp2 = (refVolt * refVolt);
#if defined(__LP64__) || defined(_LP64)
			newPower = temp1 / temp2;
#else
			newPower = div64_u64(temp1, temp2);
#endif
			upower_tbl_ref[i].row[j].dyn_pwr = newPower;
			/* upower_debug("dyn_pwr= %u\n",
			 * upower_tbl_ref[i].row[j].dyn_pwr);
			 */
		}
	}
}

static void upower_update_lkg_pwr(void)
{
	int i, j, k;
	struct upower_tbl *tbl;
#ifndef EARLY_PORTING_SPOWER
	unsigned int spower_bank_id;
	unsigned int volt;
	int degree;
	unsigned int temp;
#endif

	for (i = 0; i < NR_UPOWER_BANK; i++) {
		tbl = upower_tbl_infos[i].p_upower_tbl;

#ifdef EARLY_PORTING_SPOWER
		/* get p-state lkg */
		for (j = 0; j < UPOWER_OPP_NUM; j++) {
			for (k = 0; k < NR_UPOWER_DEGREE; k++)
				upower_tbl_ref[i].row[j].lkg_pwr[k] =
					tbl->row[j].lkg_pwr[k];
		}

		/* get c-state lkg */
		for (j = 0; j < NR_UPOWER_DEGREE; j++) {
			for (k = 0; k < NR_UPOWER_CSTATES; k++)
				upower_tbl_ref[i].idle_states[j][k].power =
					tbl->idle_states[j][k].power;
		}
#else
		spower_bank_id = upower_bank_to_spower_bank(i);

#if 0
		upower_debug("upower bank, spower bank= %d, %d\n",
			i, spower_bank_id);
		upower_debug("deg = %d, %d, %d, %d, %d, %d\n",
			degree_set[0], degree_set[1],
		degree_set[2], degree_set[3], degree_set[4], degree_set[5]);
#endif

		/* wrong bank */
		if (spower_bank_id == -1)
			continue;

		/* get p-state lkg */
		for (j = 0; j < UPOWER_OPP_NUM; j++) {
			volt = (unsigned int)upower_tbl_ref[i].row[j].volt;
			for (k = 0; k < NR_UPOWER_DEGREE; k++) {
				degree = degree_set[k];
				/* get leakage from spower driver and transfer
				 * mw to uw
				 */
				temp = mt_spower_get_leakage(
					spower_bank_id, (volt / 100), degree);
				upower_tbl_ref[i].row[j].lkg_pwr[k] =
					temp * 1000;
#if 0
				upower_debug("deg[%d] temp[%u] lkg_pwr[%u]\n",
					degree, temp,
					upower_tbl_ref[i].row[j].lkg_pwr[k]);
#endif
			}
#if 0
			upower_debug
			("volt[%u] lkg_pwr[%u, %u, %u, %u, %u, %u]\n", volt,
				upower_tbl_ref[i].row[j].lkg_pwr[0],
				upower_tbl_ref[i].row[j].lkg_pwr[1],
				upower_tbl_ref[i].row[j].lkg_pwr[2],
				upower_tbl_ref[i].row[j].lkg_pwr[3],
				upower_tbl_ref[i].row[j].lkg_pwr[4],
				upower_tbl_ref[i].row[j].lkg_pwr[5]);
#endif
		}

		/* get c-state lkg */
		upower_tbl_ref[i].nr_idle_states = NR_UPOWER_CSTATES;
		volt = UPOWER_C1_VOLT;
		for (j = 0; j < NR_UPOWER_DEGREE; j++) {
			for (k = 0; k < NR_UPOWER_CSTATES; k++) {
				/* if c1 state, query lkg from lkg driver */
				if (k == UPOWER_C1_IDX) {
					degree = degree_set[j];
					/* get leakage from spower driver and
					 * transfer mw to uw
					 */
					temp = mt_spower_get_leakage(
						spower_bank_id, (volt / 100),
						degree);
					upower_tbl_ref[i].idle_states[j]
								     [k].power =
						(unsigned long)(temp * 1000);
				} else {
					upower_tbl_ref[i].idle_states[j]
								     [k].power =
						tbl->idle_states[j][k].power;
				}
			}
		}
#endif
	}
}

static void upower_init_cap(void)
{
	int i, j;
	struct upower_tbl *tbl;

	for (i = 0; i < NR_UPOWER_BANK; i++) {
		tbl = upower_tbl_infos[i].p_upower_tbl;
		for (j = 0; j < UPOWER_OPP_NUM; j++)
			upower_tbl_ref[i].row[j].cap = tbl->row[j].cap;
	}
}

static void upower_init_rownum(void)
{
	int i;

	for (i = 0; i < NR_UPOWER_BANK; i++)
		upower_tbl_ref[i].row_num = UPOWER_OPP_NUM;
}

static unsigned int eem_is_enabled(void)
{
#ifndef EEM_DISABLE

	return 0;
#endif
}

static void upower_wait_for_eem_volt_done(void)
{
#ifndef EEM_NOT_SET_VOLT
	unsigned char eem_volt_not_ready = 0;
	int i;

	udelay(100);
	while (1) {
		eem_volt_not_ready = 0;
		for (i = 0; i < NR_UPOWER_BANK; i++) {
			if (upower_tbl_ref[i].row[UPOWER_OPP_NUM - 1].volt == 0)
				eem_volt_not_ready = 1;
		}
		if (!eem_volt_not_ready)
			break;
		/* if eem volt not ready, wait 100us */
		udelay(100);
	}
#endif
}

static void upower_init_lkgidx(void)
{
	int i;

	for (i = 0; i < NR_UPOWER_BANK; i++)
		upower_tbl_ref[i].lkg_idx = DEFAULT_LKG_IDX;
}

static void upower_init_volt(void)
{
	int i, j;
	struct upower_tbl *tbl;

	for (i = 0; i < NR_UPOWER_BANK; i++) {
		tbl = upower_tbl_infos[i].p_upower_tbl;
		for (j = 0; j < UPOWER_OPP_NUM; j++)
			upower_tbl_ref[i].row[j].volt = tbl->row[j].volt;
	}
}

static int upower_update_tbl_ref(void)
{
	int i;
	int ret = 0;

	/* To disable upower, do not update upower ptr*/
	if (!upower_enable) {
		upower_error("upower is disabled\n");
		return 0;
	}

#ifdef UPOWER_PROFILE_API_TIME
	upower_get_start_time_us(UPDATE_TBL_PTR);
#endif

	new_p_tbl_infos =
		kzalloc(sizeof(*new_p_tbl_infos) * NR_UPOWER_BANK, GFP_KERNEL);
	if (!new_p_tbl_infos) {
		upower_error("Out of mem to create new_p_tbl_infos\n");
		return -ENOMEM;
	}

	/* upower_tbl_ref is the ptr points to table in sram */
	for (i = 0; i < NR_UPOWER_BANK; i++) {
		new_p_tbl_infos[i].p_upower_tbl = &upower_tbl_ref[i];
		new_p_tbl_infos[i].name = upower_tbl_infos[i].name;
		/* upower_debug("new_p_tbl_infos[%d].name = %s\n", i,
		 * new_p_tbl_infos[i].name);
		 */
	}

#ifdef UPOWER_RCU_LOCK
	rcu_assign_pointer(p_upower_tbl_infos, new_p_tbl_infos);
/* synchronize_rcu();*/
#else
	p_upower_tbl_infos = new_p_tbl_infos;
#endif

#ifdef UPOWER_PROFILE_API_TIME
	upower_get_diff_time_us(UPDATE_TBL_PTR);
	print_diff_results(UPDATE_TBL_PTR);
#endif

	return ret;
}

static void get_L_pwr_efficiency(void)
{

#ifndef DISABLE_TP
	int i;
	unsigned int max = 0;
	unsigned int min = ~0U;
	unsigned long long sum;
	struct upower_tbl *tbl;

	for (i = 0; i < UPOWER_OPP_NUM; i++) {
		tbl = &upower_tbl_ref[UPOWER_BANK_L];
		sum = (unsigned long long)(tbl->row[i].lkg_pwr[LKG_IDX] +
				tbl->row[i].dyn_pwr);
#if defined(__LP64__) || defined(_LP64)
		tbl->row[i].pwr_efficiency =
			sum / (unsigned long long)tbl->row[i].cap;

#else
		tbl->row[i].pwr_efficiency =
			div64_u64(sum, (unsigned long long)tbl->row[i].cap);
#endif

		upower_debug("L[%d] eff = %d dyn = %d lkg = %d cap = %d\n",
			i, tbl->row[i].pwr_efficiency,
			tbl->row[i].dyn_pwr,
			tbl->row[i].lkg_pwr[LKG_IDX],
			tbl->row[i].cap
			);

		if (tbl->row[i].pwr_efficiency > max)
			max = tbl->row[i].pwr_efficiency;
		if (tbl->row[i].pwr_efficiency < min)
			min = tbl->row[i].pwr_efficiency;
	}

	tbl->max_efficiency = max;
	tbl->min_efficiency = min;
#endif
}

static void get_LL_pwr_efficiency(void)
{

#ifndef DISABLE_TP
	int i;
	unsigned int max = 0;
	unsigned int min = ~0U;
	unsigned long long LL_pwr, CCI_pwr;
	unsigned long long sum;
	struct upower_tbl *tbl, *ctbl;

	tbl = &upower_tbl_ref[UPOWER_BANK_LL];
	ctbl = &upower_tbl_ref[UPOWER_BANK_CCI];
	for (i = 0; i < UPOWER_OPP_NUM; i++) {
		LL_pwr = (unsigned long long)(tbl->row[i].lkg_pwr[LKG_IDX] +
				tbl->row[i].dyn_pwr);
		CCI_pwr = (unsigned long long)(ctbl->row[i].lkg_pwr[LKG_IDX] +
				ctbl->row[i].dyn_pwr);
		sum = (unsigned long long)LL_CORE_NUM * LL_pwr + CCI_pwr;
#if defined(__LP64__) || defined(_LP64)
		tbl->row[i].pwr_efficiency =
		sum / (unsigned long long)(LL_CORE_NUM * tbl->row[i].cap);

#else
		tbl->row[i].pwr_efficiency =
			div64_u64(LL_CORE_NUM * LL_pwr + CCI_pwr,
			(unsigned long long)(LL_CORE_NUM * tbl->row[i].cap))
#endif

		upower_debug("LL[%d] eff = %d dyn = %d lkg = %d cap = %d\n",
			i, tbl->row[i].pwr_efficiency,
			tbl->row[i].dyn_pwr,
			tbl->row[i].lkg_pwr[LKG_IDX],
			tbl->row[i].cap
			);

		if (tbl->row[i].pwr_efficiency > max)
			max = tbl->row[i].pwr_efficiency;
		if (tbl->row[i].pwr_efficiency < min)
			min = tbl->row[i].pwr_efficiency;
	}

	tbl->max_efficiency = max;
	tbl->min_efficiency = min;
#endif
}
static int upower_cal_turn_point(void)
{
	int i;
#ifndef DISABLE_TP
	struct upower_tbl *L_tbl, *LL_tbl;
	int tempLL;
	int find_flag = 0;

	L_tbl = &upower_tbl_ref[UPOWER_BANK_L];
	LL_tbl = &upower_tbl_ref[UPOWER_BANK_LL];
	/* calculate turn point */
	for (i = UPOWER_OPP_NUM - 1; i >= 0 ; i--) {
		tempLL = LL_tbl->row[i].pwr_efficiency;
		upower_debug("@@LL_effi[%d] = %d , L_min_effi = %d\n",
				i, tempLL, L_tbl->min_efficiency);
		if (tempLL <= L_tbl->min_efficiency) {
			L_tbl->turn_point = i + 1;
			LL_tbl->turn_point = i + 1;
			find_flag = 1;
			break;
		}

	}
	if (!find_flag) {
		L_tbl->turn_point = UPOWER_OPP_NUM;
		LL_tbl->turn_point = UPOWER_OPP_NUM;
		i = UPOWER_OPP_NUM;
	}
#else
	i = -1;
#endif
	return i;

}

#ifdef UPOWER_USE_QOS_IPI
#if UPOWER_ENABLE_TINYSYS_SSPM
void upower_send_data_ipi(phys_addr_t phy_addr, unsigned long long size)
{
#if defined(CONFIG_MACH_MT6771)
	struct qos_data qos_d;
#else
	struct qos_ipi_data qos_d;
#endif

	qos_d.cmd = QOS_IPI_UPOWER_DATA_TRANSFER;
	qos_d.u.upower_data.arg[0] = phy_addr;
	qos_d.u.upower_data.arg[1] = size;
	qos_ipi_to_sspm_command(&qos_d, 3);
}

void upower_dump_data_ipi(void)
{
#if defined(CONFIG_MACH_MT6771)
	struct qos_data qos_d;
#else
	struct qos_ipi_data qos_d;
#endif

	qos_d.cmd = QOS_IPI_UPOWER_DUMP_TABLE;
	qos_ipi_to_sspm_command(&qos_d, 1);
}
#endif
#endif

static int __init upower_get_tbl_ref(void)
{
#if UPOWER_ENABLE_TINYSYS_SSPM
	int i;
	unsigned char *ptr;
#endif

#ifdef UPOWER_NOT_READY
	return 0;
#endif
	/* get raw upower table and target upower table location */
	get_original_table();

#if UPOWER_ENABLE_TINYSYS_SSPM
	/* get sspm reserved mem */
	upower_data_phy_addr = sspm_reserve_mem_get_phys(UPD_MEM_ID);
	upower_data_virt_addr = sspm_reserve_mem_get_virt(UPD_MEM_ID);
	upower_data_size = sspm_reserve_mem_get_size(UPD_MEM_ID);

	upower_debug("phy_addr = 0x%llx, virt_addr=0x%llx\n",
		     (unsigned long long)upower_data_phy_addr,
		     (unsigned long long)upower_data_virt_addr);

	/* clear */
	ptr = (unsigned char *)(uintptr_t)upower_data_virt_addr;
	for (i = 0; i < upower_data_size; i++)
		ptr[i] = 0x0;

	upower_tbl_ref = (struct upower_tbl *)(uintptr_t)upower_data_virt_addr;

#ifdef UPOWER_USE_QOS_IPI
	upower_send_data_ipi(upower_data_phy_addr, upower_data_size);
#else
	/* send sspm reserved mem into sspm through eem's ipi */
	mt_eem_send_upower_table_ref(upower_data_phy_addr, upower_data_size);
#endif
#endif
	/* upower_tbl_ref has been assigned in get_original_table() if no sspm
	 */
	upower_debug("upower tbl orig location([0](%p)= %p\n", upower_tbl_infos,
		     upower_tbl_infos[0].p_upower_tbl);
	upower_debug("upower tbl new location([0](%p)\n", upower_tbl_ref);

	return 0;
}

#ifdef UPOWER_PROFILE_API_TIME
static void profile_api(void)
{
	int i, j;

	upower_debug("----profile upower_get_power()----\n");
	/* do 56*2 times */
	for (i = 0; i < NR_UPOWER_BANK; i++) {
		for (j = 0; j < UPOWER_OPP_NUM; j++) {
			upower_get_power(i, j, UPOWER_DYN);
			upower_get_power(i, j, UPOWER_LKG);
		}
	}
	upower_debug("----profile upower_update_tbl_ref()----\n");
	for (i = 0; i < 10; i++)
		upower_update_tbl_ref();
}
#endif

static int upower_debug_proc_show(struct seq_file *m, void *v)
{

	struct upower_tbl_info **addr_ptr_tbl_info;
	struct upower_tbl_info *ptr_tbl_info;
	struct upower_tbl *ptr_tbl;
	int i, j;

	/* get addr of ptr which points to upower_tbl_infos[] */
	addr_ptr_tbl_info = upower_get_tbl();
	/* get ptr which points to upower_tbl_infos[] */
	ptr_tbl_info = *addr_ptr_tbl_info;
	/* upower_debug("get upower tbl location = %p\n",
	 * ptr_tbl_info[0].p_upower_tbl);
	 */

	seq_printf(
		m,
		"ptr_tbl_info --> %p --> tbl %p (p_upower_tbl_infos --> %p)\n",
		ptr_tbl_info, ptr_tbl_info[0].p_upower_tbl, p_upower_tbl_infos);

	/* print all the tables that record in upower_tbl_infos[]*/
	for (i = 0; i < NR_UPOWER_BANK; i++) {
		seq_printf(m, "%s\n", upower_tbl_infos[i].name);
		ptr_tbl = ptr_tbl_info[i].p_upower_tbl;
		for (j = 0; j < UPOWER_OPP_NUM; j++) {
			seq_printf(m, " cap = %lu, volt = %u, dyn = %u,",
					ptr_tbl->row[j].cap,
					ptr_tbl->row[j].volt,
					ptr_tbl->row[j].dyn_pwr);
			seq_printf(m,
			" lkg = {%u, %u, %u, %u, %u, %u} pwr_efficiency = %u\n",
					ptr_tbl->row[j].lkg_pwr[0],
					ptr_tbl->row[j].lkg_pwr[1],
					ptr_tbl->row[j].lkg_pwr[2],
					ptr_tbl->row[j].lkg_pwr[3],
					ptr_tbl->row[j].lkg_pwr[4],
					ptr_tbl->row[j].lkg_pwr[5],
					ptr_tbl->row[j].pwr_efficiency);

		}
		seq_printf(m, " lkg_idx, num_row, turn_point: %d, %d, %d\n\n",
		ptr_tbl->lkg_idx, ptr_tbl->row_num, ptr_tbl->turn_point);
	}

#ifdef UPOWER_USE_QOS_IPI
#if UPOWER_ENABLE_TINYSYS_SSPM
	upower_dump_data_ipi();
#endif
#endif
	return 0;
}

#define PROC_FOPS_RW(name)                                                     \
	static int name##_proc_open(struct inode *inode, struct file *file)    \
	{                                                                      \
		return single_open(file, name##_proc_show, PDE_DATA(inode));   \
	}                                                                      \
	static const struct file_operations name##_proc_fops = {               \
		.owner = THIS_MODULE,                                          \
		.open = name##_proc_open,                                      \
		.read = seq_read,                                              \
		.llseek = seq_lseek,                                           \
		.release = single_release,                                     \
		.write = name##_proc_write,                                    \
	}

#define PROC_FOPS_RO(name)                                                     \
	static int name##_proc_open(struct inode *inode, struct file *file)    \
	{                                                                      \
		return single_open(file, name##_proc_show, PDE_DATA(inode));   \
	}                                                                      \
	static const struct file_operations name##_proc_fops = {               \
		.owner = THIS_MODULE,                                          \
		.open = name##_proc_open,                                      \
		.read = seq_read,                                              \
		.llseek = seq_lseek,                                           \
		.release = single_release,                                     \
	}

#define PROC_ENTRY(name)                                                       \
	{                                                                      \
		__stringify(name), &name##_proc_fops                           \
	}
/* create fops */
PROC_FOPS_RO(upower_debug);

static int create_procfs(void)
{
	struct proc_dir_entry *upower_dir = NULL;
	int i = 0;

	struct pentry {
		const char *name;
		const struct file_operations *fops;
	};

	struct pentry upower_entries[] = {
		/* {__stringify(name), &name ## _proc_fops} */
		PROC_ENTRY(upower_debug),
	};

	/* To disable upower, do not create procfs*/
	if (!upower_enable)
		return 0;

	/* create proc/upower node */
	upower_dir = proc_mkdir("upower", NULL);
	if (!upower_dir) {
		upower_error("[%s] mkdir /proc/upower failed\n", __func__);
		return -1;
	}

	for (i = 0; i < ARRAY_SIZE(upower_entries); i++) {
		if (!proc_create(upower_entries[i].name,
				 0664, upower_dir,
				 upower_entries[i].fops)) {
			upower_error("[%s]: create /proc/upower/%s failed\n",
				     __func__, upower_entries[i].name);
			return -3;
		}
	}
	return 0;
}

static int __init upower_init(void)
{
	int turn;
#ifdef UPOWER_NOT_READY
	return 0;
#endif

/* PTP has no efuse, so volt will be set to orig data
 * before upower_init_volt(), PTP has called upower_update_volt_by_eem()
 */
#if 0
	get_original_table();
	upower_debug("upower tbl orig location([0](%p)= %p\n",
	upower_tbl_infos, upower_tbl_infos[0].p_upower_tbl);
#endif

#ifdef UPOWER_UT
	upower_debug("--------- (UT)before tbl ready--------------\n");
	upower_ut();
#endif

	/* init rownum to UPOWER_OPP_NUM*/
	upower_init_rownum();

	upower_init_cap();

	/* apply orig volt and lkgidx, if eem is not enabled*/
	if (!eem_is_enabled()) {
		upower_error("eem is not enabled\n");
		upower_init_lkgidx();
		upower_init_volt();
	} else {
		upower_wait_for_eem_volt_done();
	}

	upower_update_dyn_pwr();
	upower_update_lkg_pwr();
	get_L_pwr_efficiency();
	get_LL_pwr_efficiency();
	turn = upower_cal_turn_point();
	set_sched_turn_point_cap();
	upower_debug("@@~turn point is %d\n", turn);
#ifdef UPOWER_L_PLUS
	upower_update_L_plus_cap();
	upower_update_L_plus_lkg_pwr();
#endif
	upower_update_tbl_ref();

#ifdef UPOWER_UT
	upower_debug("--------- (UT)tbl ready--------------\n");
	upower_ut();
#endif

#ifdef UPOWER_PROFILE_API_TIME
	profile_api();
#endif

	create_procfs();

	/* print_tbl(); */
	return 0;
}
#ifdef __KERNEL__
subsys_initcall(upower_get_tbl_ref);
late_initcall(upower_init);
#endif
MODULE_DESCRIPTION("MediaTek Unified Power Driver v1.0");
MODULE_LICENSE("GPL");
