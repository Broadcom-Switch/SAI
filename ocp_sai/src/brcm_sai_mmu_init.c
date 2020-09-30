/*********************************************************************
 *
 * Copyright: (c) 2017 Broadcom.
 * Broadcom Proprietary and Confidential. All rights reserved.
 *
 *********************************************************************/

#include <sai.h>
#include <brcm_sai_common.h>



/*
  ################################################################################
  #                             TH/TH2 CPU Pool functions                        #
  ################################################################################
*/

/* Ingress Pool 1 for CPU bound traffic */
static const  sai_attribute_t cpu_ing_pool_attr[] = {
    {SAI_BUFFER_POOL_ATTR_SIZE, .value.u32 = 803712},
    {SAI_BUFFER_POOL_ATTR_TYPE, .value.s32 = SAI_BUFFER_POOL_TYPE_INGRESS},
    {SAI_BUFFER_POOL_ATTR_THRESHOLD_MODE, .value.s32 = SAI_BUFFER_POOL_THRESHOLD_MODE_STATIC},
    {SAI_BUFFER_POOL_ATTR_BRCM_CUSTOM_POOL_ID, .value.u8 = BRCM_SAI_CPU_INGRESS_POOL_DEFAULT}
};

/* CPU traffic ingress profile for front panel ports */
static  sai_attribute_t fp_ing_profile_attr[] = {
    {SAI_BUFFER_PROFILE_ATTR_POOL_ID, .value.oid = 0},
    {SAI_BUFFER_PROFILE_ATTR_THRESHOLD_MODE, .value.s32 = SAI_BUFFER_PROFILE_THRESHOLD_MODE_STATIC},
    {SAI_BUFFER_PROFILE_ATTR_XON_TH, .value.u32 = 0},
    {SAI_BUFFER_PROFILE_ATTR_BUFFER_SIZE, .value.s32 = 0},
    {SAI_BUFFER_PROFILE_ATTR_SHARED_STATIC_TH, .value.s32 = 200928},
};

/* egress Pool 2 for CPU Tx traffic */
static const sai_attribute_t cpu_egr_pool_attr[] = {
    {SAI_BUFFER_POOL_ATTR_SIZE, .value.u32 = 614016},
    {SAI_BUFFER_POOL_ATTR_TYPE, .value.s32 = SAI_BUFFER_POOL_TYPE_EGRESS},
    {SAI_BUFFER_POOL_ATTR_THRESHOLD_MODE, .value.s32 = SAI_BUFFER_POOL_THRESHOLD_MODE_DYNAMIC},
    {SAI_BUFFER_POOL_ATTR_BRCM_CUSTOM_POOL_ID, .value.u8 = BRCM_SAI_CPU_EGRESS_POOL_DEFAULT}
};


/* FP traffic egress profile for  queues */
static sai_attribute_t cpu_fp_egr_profile_attr[] = {
    {SAI_BUFFER_PROFILE_ATTR_POOL_ID, .value.oid = 0},
    {SAI_BUFFER_PROFILE_ATTR_THRESHOLD_MODE, .value.s32 = SAI_BUFFER_PROFILE_THRESHOLD_MODE_DYNAMIC},
    {SAI_BUFFER_PROFILE_ATTR_BUFFER_SIZE, .value.s32 = 8 * 208},
    {SAI_BUFFER_PROFILE_ATTR_SHARED_DYNAMIC_TH, .value.s32 = -1},
};

sai_object_id_t cpu_ingress_profile_id;
sai_object_id_t cpu_fp_ingress_profile_id;
sai_object_id_t cpu_fp_egress_profile_id;

/* Needs to be set by application */
static int _brcm_sai_cpu_max_hi_pri_queue = 4;

static sai_status_t
set_pg_profile(int pg, sai_object_id_t port_id,
               sai_object_id_t profile_id)
{
    sai_status_t rv;
    int no_of_pg_per_port;
    sai_attribute_t attr;
    sai_object_id_t obj_list[8];
    sai_attribute_t pg_id_list;

    // Get pg list
    attr.id = SAI_PORT_ATTR_NUMBER_OF_INGRESS_PRIORITY_GROUPS;
    attr.value.u32 = 0;
    rv = port_apis.get_port_attribute(port_id, 1, &attr);
    if (SAI_STATUS_SUCCESS != rv)
    {
        BRCM_SAI_LOG_MMU_BUFFER(SAI_LOG_LEVEL_ERROR,
                                "Failed to call get_port_attribute "
                                "num priority groups: %d\n", rv);
        return rv;
    }
    no_of_pg_per_port = attr.value.u32;

    pg_id_list.id = SAI_PORT_ATTR_INGRESS_PRIORITY_GROUP_LIST;
    pg_id_list.value.objlist.count = no_of_pg_per_port;
    pg_id_list.value.objlist.list = obj_list;
    rv = port_apis.get_port_attribute(port_id, 1, &pg_id_list);
    if (SAI_STATUS_SUCCESS != rv)
    {
        BRCM_SAI_LOG_MMU_BUFFER(SAI_LOG_LEVEL_ERROR,
                                "Failure in get_port_attribute "
                                "pg list: %d\n", rv);
        return rv;
    }

    attr.id = SAI_INGRESS_PRIORITY_GROUP_ATTR_BUFFER_PROFILE;
    attr.value.oid = profile_id;
    rv = buffer_apis.set_ingress_priority_group_attribute(pg_id_list.value.objlist.list[pg], &attr);
    if (rv != SAI_STATUS_SUCCESS)
    {
        BRCM_SAI_LOG_MMU_BUFFER(SAI_LOG_LEVEL_ERROR,
                                "Failure in set_ingress_priority_group_attr: %d\n",
                                rv);
        return rv;
    }
    return rv;
}

static sai_status_t
set_queue_profile(int queue,
                  sai_object_id_t port_id,
                  sai_object_id_t profile_id)
{
    sai_status_t rv;
    sai_attribute_t attr;
    sai_object_id_t* obj_list;
    sai_attribute_t sai_queue_list;
    int queue_count;

    attr.id = SAI_PORT_ATTR_QOS_NUMBER_OF_QUEUES;
    attr.value.u32 = 0;
    rv = port_apis.get_port_attribute(port_id, 1, &attr);
    if (SAI_STATUS_SUCCESS != rv)
    {
        BRCM_SAI_LOG_MMU_BUFFER(SAI_LOG_LEVEL_ERROR,
                                "Failed to call get_port_attribute "
                                "num queues: %d\n", rv);
        return rv;
    }
    queue_count = attr.value.u32;
    sai_queue_list.id = SAI_PORT_ATTR_QOS_QUEUE_LIST;
    sai_queue_list.value.objlist.count = queue_count;
    obj_list = calloc(queue_count, sizeof(sai_object_id_t));
    sai_queue_list.value.objlist.list = obj_list;

    /* Get queue list */
    rv = port_apis.get_port_attribute(port_id, 1,
                                      &sai_queue_list);
    if (SAI_STATUS_SUCCESS != rv)
    {
        BRCM_SAI_LOG_MMU_BUFFER(SAI_LOG_LEVEL_ERROR,
                                "Failed to call get_port_attribute "
                                "queues list: %d\n", rv);
        return rv;
    }

    attr.id = SAI_QUEUE_ATTR_BUFFER_PROFILE_ID;
    attr.value.oid = profile_id;
    rv = qos_apis.set_queue_attribute(sai_queue_list.value.objlist.list[queue],
                                      &attr);
    if (rv != SAI_STATUS_SUCCESS)
    {
        BRCM_SAI_LOG_MMU_BUFFER(SAI_LOG_LEVEL_ERROR,
                                "Failure in set_queue_attribute "
                                "buff prof: %d\n", rv);
        return rv;
    }
    return rv;
}

/*
 * Routine Description:
 *   Needed to configure CPU related ingress/egress resources
 *
 * Arguments: None
 *
 * Return Values:
 *   SAI status code.
 */
sai_status_t
_brcm_sai_mmu_cpu_init()
{
    sai_status_t rv;
    sai_object_id_t ports[_BRCM_SAI_MAX_PORTS];
    sai_object_list_t port_list = { _BRCM_SAI_MAX_PORTS, ports };
    sai_attribute_t fp_list = { SAI_SWITCH_ATTR_PORT_LIST,
                                .value.objlist = port_list};
    sai_object_id_t cpu_port;
    int i =0;
    sai_object_id_t pool_id, switch_id;
    bcm_info_t info;

    if (FALSE == _brcm_sai_switch_is_inited())
    {
        BRCM_SAI_LOG_MMU_BUFFER(SAI_LOG_LEVEL_ERROR,
                                "Error configuring CPU Pools"
                                " - Switch not initialized \n");
        return SAI_STATUS_FAILURE;
    }

    rv = bcm_info_get(0, &info);
    if (BCM_FAILURE(rv))
    {
        BRCM_SAI_LOG_MMU_BUFFER(SAI_LOG_LEVEL_ERROR,
                                "Error configuring CPU Pools"
                                " - Switch info failure \n");
        return SAI_STATUS_FAILURE;
    }
    switch_id =  BRCM_SAI_CREATE_OBJ_SUB_MAP(SAI_OBJECT_TYPE_SWITCH,
                                             info.revision,
                                             info.device, 0);

    /* Create ingress pool 1 */
    rv = buffer_apis.create_buffer_pool(&pool_id,
                                        switch_id,
                                        4,
                                        cpu_ing_pool_attr);
    BRCM_SAI_API_CHK(SAI_API_BUFFER, "CPU Ingress Pool create", rv);

    fp_ing_profile_attr[0].value.oid = pool_id;
    /* Create buffer profile */
    rv = buffer_apis.create_buffer_profile(&cpu_ingress_profile_id,
                                           switch_id,
                                           5,
                                           fp_ing_profile_attr);
    BRCM_SAI_API_CHK(SAI_API_BUFFER, "Front Panel CPU Ingress Profile create", rv);


    /* Create egress pool  */
    rv = buffer_apis.create_buffer_pool(&pool_id,
                                        switch_id,
                                        4,
                                        cpu_egr_pool_attr);
    BRCM_SAI_API_CHK(SAI_API_BUFFER, "CPU Egress Pool create", rv);
    /* Create buffer profile */
    cpu_fp_egr_profile_attr[0].value.oid = pool_id;
    rv = buffer_apis.create_buffer_profile(&cpu_fp_egress_profile_id,
                                           switch_id,
                                           4,
                                           cpu_fp_egr_profile_attr);
    BRCM_SAI_API_CHK(SAI_API_BUFFER, "Front Panel CPU Egress Profile create", rv);

    /* Now apply profiles on CPU and FP pg/queues */
    rv = _brcm_sai_get_switch_attribute(1, &fp_list);
    BRCM_SAI_API_CHK(SAI_API_BUFFER, "Switch port list get", rv);

    for (i=0; i< fp_list.value.objlist.count; i++)
    {
        /* Apply buffer profile on CPU_PG on each front panel port */
        rv = set_pg_profile(_brcm_sai_cpu_pg_id,
                            fp_list.value.objlist.list[i],
                            cpu_ingress_profile_id);
        /* Apply queue profile on egress queue 7 on each front panel
           port */
        rv = set_queue_profile(BRCM_SAI_CPU_EGRESS_QUEUE_DEFAULT,
                               fp_list.value.objlist.list[i],
                               cpu_fp_egress_profile_id);
    }
    /* Apply profiles on CPU port */
    cpu_port = BRCM_SAI_CREATE_OBJ(SAI_OBJECT_TYPE_PORT, 0);
    for(i=0;i<=9;i++)
    {
        /* Ingress PG */
        if (i < 8)
        {
            rv = set_pg_profile(i,
                                cpu_port,
                                cpu_ingress_profile_id);
            BRCM_SAI_API_CHK(SAI_API_BUFFER, "CPU Port Ingress Profile",
                             rv);
        }       
    }
    return rv;
}

static int driverInitEgressOBM(int unit, int index);
static int driverInitEgressTHDR(int unit, int index);
static int driverInitEgressTHDRConfig1(int unit);
static int driverInitEgressTHDMPortSP(int unit, int index);
static int driverInitEgressTHDM(int unit, int index);
static int driverInitIngressTHDI(int unit, int config);
static int driverInitEgressTHDU(int unit, int index);
static int driverInitDevicePool(int unit, int index);


/*****************************************************************
 * \brief TomahawkX function to program global pools and per port pool
 * mappings. Local stack arrays have the fields/values. Use appropriate
 * indexes to program based on device.
 *
 * Index for Tomahawk data is 0,1 and for TH2 is 2,3 for Tier0 and 1.
 *
 * \return OPENNSL_E_NONE on SUCCESS _BCM_E_XXX otherwise
 ********************************************************************/
static int driverInitDevicePool(int unit, int index)
{

#if defined (BCM_TOMAHAWK2_SUPPORT) || defined (BCM_TOMAHAWK_SUPPORT)
    uint32 rval = 0;
    int port;
    /* Index of arrays below  = TH-T0,TH-1, TH2-T0, TH2-T1 */

    int thdm_db_pool_shared_limit_p0_r[] = {0x2B1B,0x2808,0x7B33,0x7E30};
    int thdm_db_pool_shared_limit_p1_r[] = {0x491A,0x491A,0xC6D5,0xCA15};
    int thdm_db_pool_shared_limit_p2_r[] = {0x2E2,0x2E2,0x2E2,0x2E2};
    int thdm_qe_pool_shared_limit_p0_r[] = {0x367,0x329,0x28F,0x388};
    int thdm_qe_pool_shared_limit_p1_r[] = {0x5C6,0x5C6,0x422,0x5A8};
    int thdm_qe_pool_shared_limit_p2_r[] = {0xF1,0xF1,0xF1,0xF1};

    PBMP_ALL_ITER(unit, port) {

        rval = 0;
        SOC_IF_ERROR_RETURN(READ_THDI_HDRM_PORT_PG_HPIDr(unit, port, &rval));
        /* Map pg to an invalid HPID so it does not use the lossless
           HP */
        soc_reg_field_set(unit, THDI_HDRM_PORT_PG_HPIDr, &rval,
                          PG7_HPIDf,
                          1);

        SOC_IF_ERROR_RETURN(WRITE_THDI_HDRM_PORT_PG_HPIDr(unit, port, rval));
    }
    /* Set the global hdrm limit to 0 as well */
    rval = 0;
    soc_reg_field_set(unit, THDI_GLOBAL_HDRM_LIMITr,
                      &rval, GLOBAL_HDRM_LIMITf, 0);
    SOC_IF_ERROR_RETURN(soc_tomahawk_xpe_reg32_set(unit,
                                                   THDI_GLOBAL_HDRM_LIMITr,
                                                   -1, -1, 0, rval));
    /* MMU_THDM_DB_DEVICE_THR_CONFIGr */
    rval = 0;
    soc_reg_field_set(unit, MMU_THDM_DB_DEVICE_THR_CONFIGr,
                      &rval, PORTSP_COLOR_LIMIT_ENABLE_0f, 0);
    soc_reg_field_set(unit, MMU_THDM_DB_DEVICE_THR_CONFIGr,
                      &rval, PORTSP_COLOR_LIMIT_ENABLE_1f, 0);
    soc_reg_field_set(unit, MMU_THDM_DB_DEVICE_THR_CONFIGr,
                      &rval, PORTSP_COLOR_LIMIT_ENABLE_2f, 0);
    soc_reg_field_set(unit, MMU_THDM_DB_DEVICE_THR_CONFIGr,
                      &rval, POOL_COLOR_LIMIT_ENABLE_0f, 0);
    soc_reg_field_set(unit, MMU_THDM_DB_DEVICE_THR_CONFIGr,
                      &rval, POOL_COLOR_LIMIT_ENABLE_1f, 0);
    soc_reg_field_set(unit, MMU_THDM_DB_DEVICE_THR_CONFIGr,
                      &rval, POOL_COLOR_LIMIT_ENABLE_2f, 0);
    SOC_IF_ERROR_RETURN(soc_tomahawk_xpe_reg32_set(unit,
                                                   MMU_THDM_DB_DEVICE_THR_CONFIGr,
                                                   -1, -1, 0, rval));

    /* MMU_THDM_MCQE_DEVICE_THR_CONFIGr */
    rval = 0;
    soc_reg_field_set(unit, MMU_THDM_MCQE_DEVICE_THR_CONFIGr,
                      &rval, PORTSP_COLOR_LIMIT_ENABLE_0f, 0);
    soc_reg_field_set(unit, MMU_THDM_MCQE_DEVICE_THR_CONFIGr,
                      &rval, PORTSP_COLOR_LIMIT_ENABLE_1f, 0);
    soc_reg_field_set(unit, MMU_THDM_MCQE_DEVICE_THR_CONFIGr,
                      &rval, PORTSP_COLOR_LIMIT_ENABLE_2f, 0);
    soc_reg_field_set(unit, MMU_THDM_MCQE_DEVICE_THR_CONFIGr,
                      &rval, POOL_COLOR_LIMIT_ENABLE_0f, 0);
    soc_reg_field_set(unit, MMU_THDM_MCQE_DEVICE_THR_CONFIGr,
                      &rval, POOL_COLOR_LIMIT_ENABLE_1f, 0);
    soc_reg_field_set(unit, MMU_THDM_MCQE_DEVICE_THR_CONFIGr,
                      &rval, POOL_COLOR_LIMIT_ENABLE_2f, 0);
    SOC_IF_ERROR_RETURN(soc_tomahawk_xpe_reg32_set(unit,
                                                   MMU_THDM_MCQE_DEVICE_THR_CONFIGr,
                                                   -1, -1, 0, rval));

    /* MMU_THDM_DB_POOL_SHARED_LIMIT Pool 0*/
    rval  = 0;
    soc_reg_field_set(unit, MMU_THDM_DB_POOL_SHARED_LIMITr, &rval,
                      SHARED_LIMITf,  thdm_db_pool_shared_limit_p0_r[index]);
    SOC_IF_ERROR_RETURN(soc_tomahawk_xpe_reg32_set(unit,
                                                   MMU_THDM_DB_POOL_SHARED_LIMITr,
                                                   -1, -1, 0, rval));
    /* Pool 1 */
    rval = 0;
    soc_reg_field_set(unit, MMU_THDM_DB_POOL_SHARED_LIMITr, &rval,
                      SHARED_LIMITf, thdm_db_pool_shared_limit_p1_r[index]);
    SOC_IF_ERROR_RETURN(soc_tomahawk_xpe_reg32_set(unit,
                                                   MMU_THDM_DB_POOL_SHARED_LIMITr,
                                                   -1, -1, 1, rval));
    /*Pool 2*/
    rval = 0;
    soc_reg_field_set(unit, MMU_THDM_DB_POOL_SHARED_LIMITr, &rval,
                      SHARED_LIMITf, thdm_db_pool_shared_limit_p2_r[index]);
    SOC_IF_ERROR_RETURN(soc_tomahawk_xpe_reg32_set(unit,
                                                   MMU_THDM_DB_POOL_SHARED_LIMITr,
                                                   -1, -1, 2, rval));

    /* MMU_THDM_MCQE_POOL_SHARED_LIMIT Pool 0*/
    rval  = 0;
    soc_reg_field_set(unit, MMU_THDM_MCQE_POOL_SHARED_LIMITr, &rval,
                      SHARED_LIMITf,
                      thdm_qe_pool_shared_limit_p0_r[index]);
    SOC_IF_ERROR_RETURN(soc_tomahawk_xpe_reg32_set(unit,
                                                   MMU_THDM_MCQE_POOL_SHARED_LIMITr,
                                                   -1, -1, 0, rval));
    /* Pool 1 */
    rval = 0;
    soc_reg_field_set(unit, MMU_THDM_MCQE_POOL_SHARED_LIMITr, &rval,
                      SHARED_LIMITf,
                      thdm_qe_pool_shared_limit_p1_r[index]);
    SOC_IF_ERROR_RETURN(soc_tomahawk_xpe_reg32_set(unit,
                                                   MMU_THDM_MCQE_POOL_SHARED_LIMITr,
                                                   -1, -1, 1, rval));
    /*Pool 2*/
    rval = 0;
    soc_reg_field_set(unit, MMU_THDM_MCQE_POOL_SHARED_LIMITr, &rval,
                      SHARED_LIMITf,
                      thdm_qe_pool_shared_limit_p2_r[index]);
    SOC_IF_ERROR_RETURN(soc_tomahawk_xpe_reg32_set(unit,
                                                   MMU_THDM_MCQE_POOL_SHARED_LIMITr,
                                                   -1, -1, 2, rval));

#endif
    return BCM_E_NONE;
}



/*****************************************************************
 * \brief TomahawkX  specific function to program MMU memories once
 * at init.
 *
 * \return OPENNSL_E_NONE on SUCCESS _BCM_E_XXX otherwise
 ********************************************************************/
static int driverInitIngressTHDI(int unit, int index)
{
#if defined (BCM_TOMAHAWK2_SUPPORT) || defined (BCM_TOMAHAWK_SUPPORT)
    /* Common settings */
    soc_info_t *si;
    int port, pg, pipe, numpg, midx;
    int mem0;
    uint32 entry0[SOC_MAX_MEM_WORDS];

    si = &SOC_INFO(unit);

    PBMP_ALL_ITER(unit, port) {
        pipe = si->port_pipe[port];
        mem0 = DRV_MEM_UNIQUE_ACC(THDI_PORT_PG_CONFIGm, pipe);
        numpg = 8;
        if (mem0 != INVALIDm) {
            for (pg = 0 ; pg < numpg;pg++)
            {
                midx = DRV_TH_MMU_PIPED_MEM_INDEX(port,
                                                  THDI_PORT_PG_CONFIGm, pg);
                if (midx < 0)
                {
                    return BCM_E_PARAM;
                }
                sal_memset(entry0, 0, sizeof(entry0));
                soc_mem_field32_set(unit, mem0, entry0,
                                    PG_GBL_HDRM_ENf, 0);
                SOC_IF_ERROR_RETURN
                  (soc_mem_write(0, mem0, MEM_BLOCK_ALL, midx, &entry0));
            }
        }
    }
#endif
    return BCM_E_NONE;
}



/*****************************************************************
 * \brief TomahawkX  specific function to program MMU memories once
 * at init.
 *
 * \return OPENNSL_E_NONE on SUCCESS _BCM_E_XXX otherwise
 ********************************************************************/
static int driverInitEgressTHDU(int unit, int index)
{
#if defined (BCM_TOMAHAWK2_SUPPORT) || defined (BCM_TOMAHAWK_SUPPORT)
    /* Common settings */
    soc_info_t *si;
    int port, queue, pipe, numq, base;
    int mem0,mem1;
    uint32 entry0[SOC_MAX_MEM_WORDS], entry1[SOC_MAX_MEM_WORDS];

    si = &SOC_INFO(unit);

    PBMP_ALL_ITER(unit, port) {
        if (port == 0)
        {
            /* no UC queues */
            continue;
        }
        pipe = si->port_pipe[port];
        mem0 = SOC_MEM_UNIQUE_ACC(unit,MMU_THDU_CONFIG_QUEUEm)[pipe];
        mem1 = SOC_MEM_UNIQUE_ACC(unit,MMU_THDU_Q_TO_QGRP_MAPm)[pipe];
        numq = 9;
        base = si->port_uc_cosq_base[port];
        if (mem0 != INVALIDm && mem1 != INVALIDm) {
            for (queue = 0 ; queue <= numq; queue++)
            {
                sal_memset(entry0, 0, sizeof(entry0));
                sal_memset(entry1, 0, sizeof(entry1));
                soc_mem_field32_set(unit, mem0, entry0,
                                    Q_LIMIT_DYNAMIC_CELLf, 1);
                soc_mem_field32_set(unit, mem0, entry0,
                                    Q_COLOR_LIMIT_DYNAMIC_CELLf, 0);
                soc_mem_field32_set(unit, mem1, entry1,
                                    Q_COLOR_ENABLE_CELLf, 0);
                soc_mem_field32_set(unit, mem1, entry1,
                                    Q_LIMIT_ENABLEf, 1);
                /* Disable qgroups */
                soc_mem_field32_set(unit, mem1, entry1,
                                    QGROUP_VALIDf, 0);
                soc_mem_field32_set(unit, mem1, entry1,
                                    USE_QGROUP_MINf, 0);

                SOC_IF_ERROR_RETURN
                  (soc_mem_write(unit, mem0, MEM_BLOCK_ALL, base + queue,
                                 entry0));
                SOC_IF_ERROR_RETURN
                  (soc_mem_write(unit, mem1, MEM_BLOCK_ALL, base + queue,
                                 entry1));

            } /* queues */
        }
    }
#endif
    return BCM_E_NONE;
}


/*****************************************************************
 * \brief Tomahawk2 specific function to program MMU memories once
 * at init.  MMU_THDM_DB_QUEUE_CONFIGm, MMU_THDM_MCQE_QUEUE_CONFIGm,
 *
 * \return OPENNSL_E_NONE on SUCCESS _BCM_E_XXX otherwise
 ********************************************************************/
static int driverInitEgressTHDM(int unit, int index)
{
#if defined (BCM_TOMAHAWK_SUPPORT) || defined (BCM_TOMAHAWK2_SUPPORT)
    soc_info_t *si;
    int port, queue, pipe, numq, base;
    int mem0, mem1,mem2,mem3;
    uint32 entry0[SOC_MAX_MEM_WORDS], entry1[SOC_MAX_MEM_WORDS];
    uint32 entry2[SOC_MAX_MEM_WORDS], entry3[SOC_MAX_MEM_WORDS];

    si = &SOC_INFO(unit);

    PBMP_ALL_ITER(unit, port) {
        pipe = si->port_pipe[port];
        mem0 = SOC_MEM_UNIQUE_ACC(unit,MMU_THDM_DB_QUEUE_CONFIGm)[pipe];
        mem1 = SOC_MEM_UNIQUE_ACC(unit, MMU_THDM_MCQE_QUEUE_CONFIGm )[pipe];
        mem2 = SOC_MEM_UNIQUE_ACC(unit, MMU_THDM_DB_QUEUE_OFFSETm )[pipe];
        mem3 = SOC_MEM_UNIQUE_ACC(unit, MMU_THDM_MCQE_QUEUE_OFFSETm )[pipe];
        numq = 9;
        base = si->port_cosq_base[port];
        if ((mem0 != INVALIDm) && (mem1 != INVALIDm)) {
            for (queue = 0 ; queue <= numq; queue++)
            {
                sal_memset(entry0, 0, sizeof(entry0));
                sal_memset(entry1, 0, sizeof(entry1));
                sal_memset(entry2, 0, sizeof(entry2));
                sal_memset(entry3, 0, sizeof(entry3));
                /* MCDB common settings */
                soc_mem_field32_set(unit, mem0, entry0,
                                    Q_LIMIT_DYNAMICf, 1);
                soc_mem_field32_set(unit, mem0, entry0,
                                    Q_COLOR_LIMIT_ENABLEf, 0);
                soc_mem_field32_set(unit, mem0, entry0,
                                    Q_COLOR_LIMIT_DYNAMICf, 0);
                soc_mem_field32_set(unit, mem0, entry0,
                                    Q_LIMIT_ENABLEf, 1);
                /* MCQE common settings */
                soc_mem_field32_set(unit, mem1, entry1,
                                    Q_MIN_LIMITf, 0);
                soc_mem_field32_set(unit, mem1, entry1,
                                    Q_LIMIT_DYNAMICf, 1);
                soc_mem_field32_set(unit, mem1, entry1,
                                    Q_COLOR_LIMIT_ENABLEf, 0);
                soc_mem_field32_set(unit, mem1, entry1,
                                    Q_COLOR_LIMIT_DYNAMICf, 0);
                soc_mem_field32_set(unit, mem1, entry1,
                                    Q_LIMIT_ENABLEf, 1);
                soc_mem_field32_set(unit, mem2, entry2,
                                    RESUME_OFFSETf, 2);
                soc_mem_field32_set(unit, mem3, entry3,
                                    RESUME_OFFSETf, 1);
                /* Setting CPU port here since SAI dynamically only configures
                   UC queues */
                if (port == 0)
                {
                    soc_mem_field32_set(unit, mem0, entry0,
                                        Q_SPIDf,
                                        BRCM_SAI_CPU_EGRESS_POOL_DEFAULT);
                    soc_mem_field32_set(unit, mem1, entry1,
                                        Q_SPIDf,
                                        BRCM_SAI_CPU_EGRESS_POOL_DEFAULT);
                    if (queue < _brcm_sai_cpu_max_hi_pri_queue)
                    {
                        soc_mem_field32_set(unit, mem0, entry0,
                                            Q_MIN_LIMITf, 0x2D);
                        soc_mem_field32_set(unit, mem0, entry0,
                                            Q_SHARED_ALPHAf, 8);
                        soc_mem_field32_set(unit, mem1, entry1,
                                            Q_SHARED_ALPHAf, 8);
                    }
                    else
                    {
                        soc_mem_field32_set(unit, mem0, entry0,
                                            Q_MIN_LIMITf, 8);
                        soc_mem_field32_set(unit, mem0, entry0,
                                            Q_SHARED_ALPHAf, 5);
                        soc_mem_field32_set(unit, mem1, entry1,
                                            Q_SHARED_ALPHAf, 5);
                    }
                } /* port == 0 */

                SOC_IF_ERROR_RETURN
                  (soc_mem_write(unit, mem0, MEM_BLOCK_ALL, base + queue,
                                 entry0));
                SOC_IF_ERROR_RETURN
                  (soc_mem_write(unit, mem1, MEM_BLOCK_ALL, base + queue,
                                 entry1));
                SOC_IF_ERROR_RETURN
                  (soc_mem_write(unit, mem2, MEM_BLOCK_ALL, base + queue,
                                 entry2));
                SOC_IF_ERROR_RETURN
                  (soc_mem_write(unit, mem3, MEM_BLOCK_ALL, base + queue,
                                 entry3));
            }
        }
    }
#endif
    return BCM_E_NONE;
}

/*****************************************************************
 * \brief TomahawkX specific function to program MMU memories once
 * at init.
 * MMU_THDM_DB_PORTSP_CONFIGm, MMU_THDM_MCQE_PORTSP_CONFIGm
 * \return OPENNSL_E_NONE on SUCCESS _BCM_E_XXX otherwise
 ********************************************************************/
static int driverInitEgressTHDMPortSP(int unit, int index)
{
#if defined (BCM_TOMAHAWK2_SUPPORT) || defined (BCM_TOMAHAWK_SUPPORT)

    int db_shared_limit[]         = {0x4EC5, 0x4EC5, 0xD000, 0xD000};
    int db_shared_resume[]        = {0x9D7, 0x9D7, 0x19FE, 0x19FE};
    int db_red_shared_limit[]     = {0x9D9, 0x9D9, 0x1A00, 0x1A00};
    int db_red_shared_resume[]    = {0x9D7, 0x9D7, 0x19FE, 0x19FE};
    int db_yellow_shared_limit[]  = {0x9D9, 0x9D9, 0x1A00, 0x1A00};
    int db_yellow_shared_resume[] = {0x9D7, 0x9D7, 0x19FE, 0x19FE};

    int qe_shared_limit[]         = {0x5C6, 0x5C6, 0x422, 0x5A8};
    int qe_shared_resume[]        = {0x2E3, 0x2E3, 0x211, 0x2D4};
    int qe_red_shared_limit[]     = {0x2E3, 0x2E3, 0x211, 0x2D4};
    int qe_red_shared_resume[]    = {0x2E3, 0x2E3, 0x211, 0x2D4};
    int qe_yellow_shared_limit[]  = {0x2E3, 0x2E3, 0x211, 0x2D4};
    int qe_yellow_shared_resume[] = {0x2E3, 0x2E3, 0x211, 0x2D4};

    soc_info_t *si;
    int port, pool, pipe;
    int mem0, mem1, idx0,idx1;
    uint32 entry0[SOC_MAX_MEM_WORDS], entry1[SOC_MAX_MEM_WORDS];

    si = &SOC_INFO(unit);

    PBMP_ALL_ITER(unit, port) {
        pipe = si->port_pipe[port];
        mem0 = SOC_MEM_UNIQUE_ACC(unit, MMU_THDM_DB_PORTSP_CONFIGm)[pipe];
        mem1 = SOC_MEM_UNIQUE_ACC(unit, MMU_THDM_MCQE_PORTSP_CONFIGm )[pipe];

        for (pool=0;pool <3;pool++)
        {
            idx0 = DRV_TH_MMU_PIPED_MEM_INDEX(port,
                                              MMU_THDM_DB_PORTSP_CONFIGm,
                                              pool);
            idx1 = DRV_TH_MMU_PIPED_MEM_INDEX(port,
                                              MMU_THDM_MCQE_PORTSP_CONFIGm,
                                              pool);
            sal_memset(entry0, 0, sizeof(entry0));
            sal_memset(entry1, 0, sizeof(entry1));
            soc_mem_field32_set(unit, mem0, entry0, SHARED_LIMITf,
                                db_shared_limit[index]);
            soc_mem_field32_set(unit, mem0, entry0, RED_SHARED_LIMITf,
                                db_red_shared_limit[index]);
            soc_mem_field32_set(unit, mem0, entry0, YELLOW_SHARED_LIMITf,
                                db_yellow_shared_limit[index]);
            soc_mem_field32_set(unit, mem0, entry0, SHARED_LIMIT_ENABLEf,
                                0);
            soc_mem_field32_set(unit, mem0, entry0, SHARED_RESUME_LIMITf,
                                db_shared_resume[index]);
            soc_mem_field32_set(unit, mem0, entry0, YELLOW_RESUME_LIMITf,
                                db_yellow_shared_resume[index]);
            soc_mem_field32_set(unit, mem0, entry0, RED_RESUME_LIMITf,
                                db_red_shared_resume[index]);
            SOC_IF_ERROR_RETURN(soc_mem_write(unit, mem0, MEM_BLOCK_ALL,
                                              idx0, entry0));

            soc_mem_field32_set(unit, mem1, entry1, SHARED_LIMITf,
                                qe_shared_limit[index]);
            soc_mem_field32_set(unit, mem1, entry1, RED_SHARED_LIMITf,
                                qe_red_shared_limit[index]);
            soc_mem_field32_set(unit, mem1, entry1, YELLOW_SHARED_LIMITf,
                                qe_yellow_shared_limit[index]);
            soc_mem_field32_set(unit, mem1, entry1, SHARED_LIMIT_ENABLEf,
                                0);
            soc_mem_field32_set(unit, mem1, entry1, SHARED_RESUME_LIMITf,
                                qe_shared_resume[index]);
            soc_mem_field32_set(unit, mem1, entry1, YELLOW_RESUME_LIMITf,
                                qe_yellow_shared_resume[index]);
            soc_mem_field32_set(unit, mem1, entry1, RED_RESUME_LIMITf,
                                qe_red_shared_resume[index]);
            SOC_IF_ERROR_RETURN(soc_mem_write(unit, mem1, MEM_BLOCK_ALL,
                                              idx1, entry1));

        }
    }
#endif
    return BCM_E_NONE;
}

/*****************************************************************
 * \brief TomahawkX specific function to program MMU memories once
 * at init.
 * MMU_THDR_DB_LIMIT_MIN_PRIQr,MMU_THDR_DB_CONFIG_PRIQr
 * \return OPENNSL_E_NONE on SUCCESS _BCM_E_XXX otherwise
 ********************************************************************/
static int driverInitEgressTHDRConfig1(int unit)
{
#if defined (BCM_TOMAHAWK2_SUPPORT) || defined (BCM_TOMAHAWK_SUPPORT)
    uint32_t rval = 0, rqe=0;
    uint32_t rval1 = 0, rval2=0, rval3=0;
    soc_reg_field_set(unit, MMU_THDR_DB_LIMIT_MIN_PRIQr,
                      &rval, MIN_LIMITf, 0x2D);
    SOC_IF_ERROR_RETURN(WRITE_MMU_THDR_DB_LIMIT_MIN_PRIQr(unit, 8, rval));
    SOC_IF_ERROR_RETURN(WRITE_MMU_THDR_DB_LIMIT_MIN_PRIQr(unit, 9, rval));
    SOC_IF_ERROR_RETURN(WRITE_MMU_THDR_DB_LIMIT_MIN_PRIQr(unit, 10, rval));

    rval = 0;


    soc_reg_field_set(unit, MMU_THDR_DB_CONFIG_PRIQr, &rval,
                      SHARED_LIMITf, 0xA);
    soc_reg_field_set(unit, MMU_THDR_DB_CONFIG_PRIQr, &rval,
                      RESET_OFFSETf, 2);
    soc_reg_field_set(unit, MMU_THDR_DB_LIMIT_COLOR_PRIQr, &rval1,
                      SHARED_RED_LIMITf, 5);
    soc_reg_field_set(unit, MMU_THDR_DB_LIMIT_COLOR_PRIQr, &rval1,
                      SHARED_YELLOW_LIMITf, 6);
    soc_reg_field_set(unit, MMU_THDR_QE_CONFIG_PRIQr, &rval2,
                      SHARED_LIMITf, 0xA);
    soc_reg_field_set(unit, MMU_THDR_QE_LIMIT_COLOR_PRIQr,
                      &rval3, SHARED_RED_LIMITf, 5);
    soc_reg_field_set(unit, MMU_THDR_QE_LIMIT_COLOR_PRIQr,
                      &rval3, SHARED_YELLOW_LIMITf, 6);
    for (rqe=0;rqe<8;rqe++)
    {
        SOC_IF_ERROR_RETURN(WRITE_MMU_THDR_DB_CONFIG_PRIQr_REG32(unit,
                                                                 rqe, rval));
        SOC_IF_ERROR_RETURN(WRITE_MMU_THDR_DB_LIMIT_COLOR_PRIQr(unit,
                                                                rqe, rval1));
        SOC_IF_ERROR_RETURN(WRITE_MMU_THDR_QE_CONFIG_PRIQr(unit,
                                                           rqe, rval2));
        SOC_IF_ERROR_RETURN(WRITE_MMU_THDR_QE_LIMIT_COLOR_PRIQr(unit, rqe,
                                                                rval3));
    }
    rval = 0;
    rval1 = 0;
    rval2 = 0 ;
    rval3 = 0;
    soc_reg_field_set(unit, MMU_THDR_DB_CONFIG_PRIQr, &rval,
                      SHARED_LIMITf, 0xA);
    soc_reg_field_set(unit, MMU_THDR_DB_CONFIG_PRIQr, &rval,
                      RESET_OFFSETf, 2);
    soc_reg_field_set(unit, MMU_THDR_DB_LIMIT_COLOR_PRIQr, &rval1,
                      SHARED_RED_LIMITf, 5);
    soc_reg_field_set(unit, MMU_THDR_DB_LIMIT_COLOR_PRIQr, &rval1,
                      SHARED_YELLOW_LIMITf, 6);
    soc_reg_field_set(unit, MMU_THDR_QE_CONFIG_PRIQr, &rval2,
                      SHARED_LIMITf, 5);
    soc_reg_field_set(unit, MMU_THDR_QE_LIMIT_COLOR_PRIQr,
                      &rval3, SHARED_RED_LIMITf, 5);
    soc_reg_field_set(unit, MMU_THDR_QE_LIMIT_COLOR_PRIQr,
                      &rval3, SHARED_YELLOW_LIMITf, 6);
    for (rqe=0;rqe<11;rqe++)
    {
        SOC_IF_ERROR_RETURN(WRITE_MMU_THDR_DB_CONFIG_PRIQr_REG32(unit,
                                                                 rqe, rval));
        SOC_IF_ERROR_RETURN(WRITE_MMU_THDR_DB_LIMIT_COLOR_PRIQr(unit,
                                                                rqe, rval1));
        SOC_IF_ERROR_RETURN(WRITE_MMU_THDR_QE_CONFIG_PRIQr(unit,
                                                           rqe, rval2));
        SOC_IF_ERROR_RETURN(WRITE_MMU_THDR_QE_LIMIT_COLOR_PRIQr(unit, rqe,
                                                                rval3));
    }
#endif
    return BCM_E_NONE;
}


/*****************************************************************
 * \brief Tomahawk2 specific function to program MMU memories once
 * at init.
 * MMU_THDR_DB_CONFIG_SPr,MMU_THDR_DB_SP_SHARED_LIMITr
 *
 * \return OPENNSL_E_NONE on SUCCESS _BCM_E_XXX otherwise
 ********************************************************************/
static int driverInitEgressTHDR(int unit,int index)
{
#if defined (BCM_TOMAHAWK2_SUPPORT) || defined (BCM_TOMAHAWK_SUPPORT)
    int db_config_shared[] = {0x2B1B, 0x2808, 0x7B33, 0x7E30};
    int db_config_resume[] = {0x563, 0x501, 0xF66, 0xFC6};
    int db_red_shared[] =    {0x35F, 0x321, 0x9A0, 0x9DC};
    int db_red_resume[] =    {0x35F, 0x321, 0x9A0, 0x9DC};
    int db_yellow_shared[] = {0x40B, 0x3C1, 0xB8D, 0xBD5};
    int db_yellow_resume[] = {0x40B, 0x3C1, 0xB8D, 0xBD5};

    int qe_config_shared[] = {0x6C, 0x6C, 0xEC, 0xEC};
    int qe_config_resume[] = {0x6C, 0x6C, 0xEC, 0xEC};
    int qe_red_shared[] =    {0x44, 0x44, 0x94, 0x94};
    int qe_red_resume[] =    {0x44, 0x44, 0x94, 0x94};
    int qe_yellow_shared[] = {0x51, 0x51, 0xB1, 0xB1};
    int qe_yellow_resume[] = {0x51, 0x51, 0xB1, 0xB1};
    uint32  rval;
    /* reg MM_THDR_DB_CONFIG_SP */
    rval = 0;
    soc_reg_field_set(unit, MMU_THDR_DB_CONFIG_SPr,
                      &rval, SHARED_LIMITf,
                      db_config_shared[index]);
    soc_reg_field_set(unit, MMU_THDR_DB_CONFIG_SPr, &rval,
                      RESUME_LIMITf,
                      db_config_resume[index]);

    /* Write these only for pool 0 */
    SOC_IF_ERROR_RETURN(soc_tomahawk_xpe_reg32_set(unit,
                                                   MMU_THDR_DB_CONFIG_SPr,
                                                   -1, -1,
                                                   0, rval));
    /* reg MMU_THDR_DB_SP_SHARED_LIMIT */
    rval = 0;
    soc_reg_field_set(unit, MMU_THDR_DB_SP_SHARED_LIMITr, &rval,
                      SHARED_RED_LIMITf,
                      db_red_shared[index]);
    soc_reg_field_set(unit, MMU_THDR_DB_SP_SHARED_LIMITr, &rval,
                      SHARED_YELLOW_LIMITf,
                      db_yellow_shared[index]);
    SOC_IF_ERROR_RETURN(soc_tomahawk_xpe_reg32_set(unit,
                                                   MMU_THDR_DB_SP_SHARED_LIMITr,
                                                   -1, -1, 0, rval));
    /* reg MMU_THDR_DB_RESUME_COLOR_LIMIT_SP */
    rval = 0;
    soc_reg_field_set(unit, MMU_THDR_DB_RESUME_COLOR_LIMIT_SPr, &rval,
                      RESUME_RED_LIMITf,
                      db_red_resume[index]);
    soc_reg_field_set(unit, MMU_THDR_DB_RESUME_COLOR_LIMIT_SPr, &rval,
                      RESUME_YELLOW_LIMITf,
                      db_yellow_resume[index]);
    SOC_IF_ERROR_RETURN(soc_tomahawk_xpe_reg32_set(unit,
                                                   MMU_THDR_DB_RESUME_COLOR_LIMIT_SPr,
                                                   -1, -1, 0, rval));
    /* reg MMU_THDR_QE_CONFIG_SP */
    rval = 0;
    soc_reg_field_set(unit, MMU_THDR_QE_CONFIG_SPr, &rval,
                      SHARED_LIMITf,
                      qe_config_shared[index]);
    soc_reg_field_set(unit, MMU_THDR_QE_CONFIG_SPr, &rval,
                      RESUME_LIMITf,
                      qe_config_resume[index]);
    SOC_IF_ERROR_RETURN(WRITE_MMU_THDR_QE_CONFIG_SPr(unit, 0, rval));

    /* reg MMU_THDR_QE_SHARED_COLOR_LIMIT */
    rval = 0;
    soc_reg_field_set(unit, MMU_THDR_QE_SHARED_COLOR_LIMIT_SPr, &rval,
                      SHARED_RED_LIMITf,
                      qe_red_shared[index]);
    soc_reg_field_set(unit, MMU_THDR_QE_SHARED_COLOR_LIMIT_SPr, &rval,
                      SHARED_YELLOW_LIMITf,
                      qe_yellow_shared[index]);
    SOC_IF_ERROR_RETURN(WRITE_MMU_THDR_QE_SHARED_COLOR_LIMIT_SPr(unit,
                                                                 0, rval));
    /* reg MMU_THDR_QE_RESUME_COLOR_LIMIT */
    rval = 0;
    soc_reg_field_set(unit, MMU_THDR_QE_RESUME_COLOR_LIMIT_SPr, &rval,
                      RESUME_RED_LIMITf,
                      qe_red_resume[index]);
    soc_reg_field_set(unit, MMU_THDR_QE_RESUME_COLOR_LIMIT_SPr, &rval,
                      RESUME_YELLOW_LIMITf,
                      qe_yellow_resume[index]);
    SOC_IF_ERROR_RETURN(WRITE_MMU_THDR_QE_RESUME_COLOR_LIMIT_SPr(unit,
                                                                 0, rval));
#endif
    return BCM_E_NONE;
}


/*****************************************************************
 * \brief Tomahawk2 specific function to program MMU memories once
 * during init.
 *
 * \return OPENNSL_E_NONE on SUCCESS _BCM_E_XXX otherwise
 ********************************************************************/
static int driverInitEgressOBM(int unit, int index)
{
#if defined (BCM_TOMAHAWK2_SUPPORT) || defined (BCM_TOMAHAWK_SUPPORT)
    static const soc_reg_t obm_fc_thresh_regs[] = {
        IDB_OBM0_FC_THRESHOLDr, IDB_OBM1_FC_THRESHOLDr,
        IDB_OBM2_FC_THRESHOLDr, IDB_OBM3_FC_THRESHOLDr,
        IDB_OBM4_FC_THRESHOLDr, IDB_OBM5_FC_THRESHOLDr,
        IDB_OBM6_FC_THRESHOLDr, IDB_OBM7_FC_THRESHOLDr,
    };
    static const soc_reg_t obm_fc_thresh1_regs[] = {
        IDB_OBM0_FC_THRESHOLD_1r, IDB_OBM1_FC_THRESHOLD_1r,
        IDB_OBM2_FC_THRESHOLD_1r, IDB_OBM3_FC_THRESHOLD_1r,
        IDB_OBM4_FC_THRESHOLD_1r, IDB_OBM5_FC_THRESHOLD_1r,
        IDB_OBM6_FC_THRESHOLD_1r, IDB_OBM7_FC_THRESHOLD_1r,
    };
    static const soc_reg_t obm_thresh_regs[] = {
        IDB_OBM0_THRESHOLDr, IDB_OBM1_THRESHOLDr,
        IDB_OBM2_THRESHOLDr, IDB_OBM3_THRESHOLDr,
        IDB_OBM4_THRESHOLDr, IDB_OBM5_THRESHOLDr,
        IDB_OBM6_THRESHOLDr, IDB_OBM7_THRESHOLDr
    };
    static const soc_reg_t obm_thresh_1_regs[] = {
        IDB_OBM0_THRESHOLD_1r, IDB_OBM1_THRESHOLD_1r,
        IDB_OBM2_THRESHOLD_1r, IDB_OBM3_THRESHOLD_1r,
        IDB_OBM4_THRESHOLD_1r, IDB_OBM5_THRESHOLD_1r,
        IDB_OBM6_THRESHOLD_1r, IDB_OBM7_THRESHOLD_1r
    };
    static const soc_mem_t obm_pri_map_mem[][4] = {
        {IDB_OBM0_PRI_MAP_PORT0m, IDB_OBM0_PRI_MAP_PORT1m,
         IDB_OBM0_PRI_MAP_PORT2m, IDB_OBM0_PRI_MAP_PORT3m
        },
        {IDB_OBM1_PRI_MAP_PORT0m, IDB_OBM1_PRI_MAP_PORT1m,
         IDB_OBM1_PRI_MAP_PORT2m, IDB_OBM1_PRI_MAP_PORT3m
        },
        {IDB_OBM2_PRI_MAP_PORT0m, IDB_OBM2_PRI_MAP_PORT1m,
         IDB_OBM2_PRI_MAP_PORT2m, IDB_OBM2_PRI_MAP_PORT3m
        },
        {IDB_OBM3_PRI_MAP_PORT0m, IDB_OBM3_PRI_MAP_PORT1m,
         IDB_OBM3_PRI_MAP_PORT2m, IDB_OBM3_PRI_MAP_PORT3m
        },
        {IDB_OBM4_PRI_MAP_PORT0m, IDB_OBM4_PRI_MAP_PORT1m,
         IDB_OBM4_PRI_MAP_PORT2m, IDB_OBM4_PRI_MAP_PORT3m
        },
        {IDB_OBM5_PRI_MAP_PORT0m, IDB_OBM5_PRI_MAP_PORT1m,
         IDB_OBM5_PRI_MAP_PORT2m, IDB_OBM5_PRI_MAP_PORT3m
        },
        {IDB_OBM6_PRI_MAP_PORT0m, IDB_OBM6_PRI_MAP_PORT1m,
         IDB_OBM6_PRI_MAP_PORT2m, IDB_OBM6_PRI_MAP_PORT3m
        },
        {IDB_OBM7_PRI_MAP_PORT0m, IDB_OBM7_PRI_MAP_PORT1m,
         IDB_OBM7_PRI_MAP_PORT2m, IDB_OBM7_PRI_MAP_PORT3m
        }
    };
    static const soc_reg_t obm_fc_config_regs[] = {
        IDB_OBM0_FLOW_CONTROL_CONFIGr, IDB_OBM1_FLOW_CONTROL_CONFIGr,
        IDB_OBM2_FLOW_CONTROL_CONFIGr, IDB_OBM3_FLOW_CONTROL_CONFIGr,
        IDB_OBM4_FLOW_CONTROL_CONFIGr, IDB_OBM5_FLOW_CONTROL_CONFIGr,
        IDB_OBM6_FLOW_CONTROL_CONFIGr, IDB_OBM7_FLOW_CONTROL_CONFIGr
    };

    /* index-based register settings */
    int discard_limit[] =     {0x1FA,0x1FA,0x1200, 0x1200};
    int lossless_discard[] =  {0x3FF,0x3FF,0x1200, 0x1200};
    int port_xoff[] =         {0xD7, 0xD7, 0xEF3, 0xEF3};
    int port_xon[] =          {0xCD, 0xCD, 0xED5, 0xED5};
    int lossless_xoff[] =     {0x32, 0x32, 0x96, 0x96};
    int lossless_xon[] =      {0x28, 0x28, 0x78, 0x78};
    int lossy_low_pri[] =     {0x64, 0x64, 0x10E, 0x10E};
    int lossy_discard[] =     {0xC4, 0xC4, 0x28E, 0x28E};
    int lossless0_pri_profile= 0x18;

    soc_info_t *si = &SOC_INFO(unit);
    int obm, port, lane, num_lanes;
    int mem, reg,  pipe;
    uint64 rval = 0;
    uint32 entry[SOC_MAX_MEM_WORDS];

    PBMP_PORT_ITER(unit, port) {
        /* Skip management, lb and cpu */
        if (IS_LB_PORT(unit, port) ||
            IS_CPU_PORT(unit, port) ||
            IS_MANAGEMENT_PORT(unit, port)) {
            continue;
        }
        num_lanes = si->port_num_lanes[port];
        pipe = si->port_pipe[port];
        /* mem OBM_PRI_MAP */
        for (obm=0; obm < 8; obm++)
        {
            for (lane=0;lane<4;lane++)
            {
                sal_memset(entry, 0, sizeof(entry));
                mem = SOC_MEM_UNIQUE_ACC(unit, obm_pri_map_mem[obm][lane])[pipe];
                SOC_IF_ERROR_RETURN
                  (soc_mem_read(unit, mem, MEM_BLOCK_ALL,
                                0, &entry));
                soc_mem_field32_set(unit, mem, entry, OFFSET3_OB_PRIORITYf,2 );
                soc_mem_field32_set(unit, mem, entry, OFFSET4_OB_PRIORITYf,2 );

                SOC_IF_ERROR_RETURN
                  (soc_mem_write(unit, mem, MEM_BLOCK_ALL, 0, entry));


                /* reg OBM_THRESHOLD */
                reg = SOC_REG_UNIQUE_ACC(unit, obm_thresh_regs[obm])[pipe];
                rval = 0;
                SOC_IF_ERROR_RETURN
                  (soc_reg_get(unit, reg, REG_PORT_ANY, lane, &rval));
                soc_reg64_field32_set(unit, reg, &rval, LOSSLESS0_DISCARDf,
                                      lossless_discard[index]);
                soc_reg64_field32_set(unit, reg, &rval, LOSSY_DISCARDf,
                                      lossy_discard[index]);
                soc_reg64_field32_set(unit, reg, &rval, LOSSY_LOW_PRIf,
                                      lossy_low_pri[index]);
                soc_reg64_field32_set(unit, reg, &rval, LOSSY_MINf, 0);
                if (DEV_IS_TH2())
                {
                    /* reg OBM_THRESHOLD_1 has 2 other fields in TH2 */
                    SOC_IF_ERROR_RETURN
                      (soc_reg_set(unit, reg, REG_PORT_ANY, lane, rval));
                    reg = SOC_REG_UNIQUE_ACC(unit, obm_thresh_1_regs[obm])[pipe];
                    rval = 0;
                    SOC_IF_ERROR_RETURN
                      (soc_reg_get(unit, reg, REG_PORT_ANY, lane, &rval));
                }

                soc_reg64_field32_set(unit, reg, &rval, LOSSLESS1_DISCARDf,
                                      lossless_discard[index]);
                if (num_lanes == 2)
                {
                    /* 50G ports only used in TH2-T0 */
                    soc_reg64_field32_set(unit, reg, &rval, DISCARD_LIMITf,
                                          0x900);
                }
                else
                {
                    soc_reg64_field32_set(unit, reg, &rval, DISCARD_LIMITf,
                                          discard_limit[index]);
                }
                SOC_IF_ERROR_RETURN
                  (soc_reg_set(unit, reg, REG_PORT_ANY, lane, rval));

                /* reg OBM_FC_THRESHOLD */
                reg = SOC_REG_UNIQUE_ACC(unit, obm_fc_thresh_regs[obm])[pipe];
                rval = 0;
                SOC_IF_ERROR_RETURN
                  (soc_reg_get(unit, reg, REG_PORT_ANY, lane, &rval));
                if (num_lanes == 2)
                {
                    /* 50G ports only used in TH2-T0 */
                    soc_reg64_field32_set(unit, reg, &rval, LOSSLESS1_XONf,
                                          0x78);
                    soc_reg64_field32_set(unit, reg, &rval, LOSSLESS1_XOFFf,
                                          0x96);
                    soc_reg64_field32_set(unit, reg, &rval, LOSSLESS0_XONf,
                                          0x78);
                    soc_reg64_field32_set(unit, reg, &rval, LOSSLESS0_XOFFf,
                                          0x96);
                }
                else
                {
                    soc_reg64_field32_set(unit, reg, &rval, LOSSLESS1_XONf,
                                          lossless_xon[index]);
                    soc_reg64_field32_set(unit, reg, &rval, LOSSLESS1_XOFFf,
                                          lossless_xoff[index]);
                    soc_reg64_field32_set(unit, reg, &rval, LOSSLESS0_XONf,
                                          lossless_xon[index]);
                    soc_reg64_field32_set(unit, reg, &rval, LOSSLESS0_XOFFf,
                                          lossless_xoff[index]);
                }
                if (DEV_IS_TH2())
                {
                    SOC_IF_ERROR_RETURN
                      (soc_reg_set(unit, reg, REG_PORT_ANY, lane, rval));

                    /* reg OBM_FC_THRESHOLD_1 has 2 other fields */
                    reg = SOC_REG_UNIQUE_ACC(unit, obm_fc_thresh1_regs[obm])[pipe];
                    rval = 0;
                    SOC_IF_ERROR_RETURN
                      (soc_reg_get(unit, reg, REG_PORT_ANY, lane, &rval));
                }

                if (num_lanes == 2)
                {
                    /* 50G ports only used in TH2-T0 */
                    soc_reg64_field32_set(unit, reg, &rval, PORT_XONf,
                                          0x5D5);
                    soc_reg64_field32_set(unit, reg, &rval, PORT_XOFFf,
                                          0x5F3);
                }
                else
                {
                    soc_reg64_field32_set(unit, reg, &rval, PORT_XONf,
                                          port_xon[index]);
                    soc_reg64_field32_set(unit, reg, &rval, PORT_XOFFf,
                                          port_xoff[index]);
                }
                SOC_IF_ERROR_RETURN
                  (soc_reg_set(unit, reg, REG_PORT_ANY, lane, rval));

                /* OBM_FC_CONFIG */
                reg = SOC_REG_UNIQUE_ACC(unit, obm_fc_config_regs[obm])[pipe];
                SOC_IF_ERROR_RETURN
                  (soc_reg_get(unit, reg, REG_PORT_ANY, lane, &rval));
                soc_reg64_field32_set(unit, reg, &rval, PORT_FC_ENf, 1);
                soc_reg64_field32_set(unit, reg, &rval, LOSSLESS0_FC_ENf,
                                      1);
                soc_reg64_field32_set(unit, reg, &rval, LOSSLESS1_FC_ENf, 0);
                soc_reg64_field32_set(unit, reg, &rval,
                                      LOSSLESS0_PRIORITY_PROFILEf,
                                      lossless0_pri_profile);
                soc_reg64_field32_set(unit, reg, &rval,
                                      LOSSLESS1_PRIORITY_PROFILEf, 0);
                SOC_IF_ERROR_RETURN
                  (soc_reg_set(unit, reg, REG_PORT_ANY, lane, rval));
            } /* lane iter */
        }
    }/* port iter */
#endif
    return BCM_E_NONE;
}

/*****************************************************************
 * \brief Tomahawk specific API to program MMU memories once during
 * init.
 *
 * \return OPENNSL_E_NONE on SUCCESS _BCM_E_XXX otherwise
 ********************************************************************/
int driverMMUInit(int unit)
{
    int config = -1;
    char* mmu_config;

    if (DEV_IS_TH2() || DEV_IS_TH())
    {
        /* detect config */
        mmu_config = sal_config_get("mmu_init_config");

        if (mmu_config != NULL)
        {
            if (strcmp("\"MSFT-TH-Tier0\"",mmu_config) == 0)
            {
                config = 0;
            }
            if (strcmp("\"MSFT-TH-Tier1\"",mmu_config) == 0)
            {
                config = 1;
            }
            /* 108x50+8x100 */
            if (strcmp("\"MSFT-TH2-Tier0\"",mmu_config) == 0)
            {
                config = 2;
            }
            /* 48x100+16x100 */
            if (strcmp("\"MSFT-TH2-Tier1\"",mmu_config) == 0)
            {
                config = 3;
            }
        }
    }

    if (config == -1)
    {
        BRCM_SAI_LOG_MMU_BUFFER(SAI_LOG_LEVEL_INFO,
                                "No MMU init done for device\n");
        return BCM_E_NONE;
    }
#ifdef TH2_CPU_POOL_SETUP
    _brcm_sai_cpu_pool_config = 1;
#endif

    SOC_IF_ERROR_RETURN(driverInitDevicePool(unit, config));
    SOC_IF_ERROR_RETURN(driverInitIngressTHDI(unit, config));
    SOC_IF_ERROR_RETURN(driverInitEgressTHDU(unit, config));
    SOC_IF_ERROR_RETURN(driverInitEgressTHDM(unit, config));
    SOC_IF_ERROR_RETURN(driverInitEgressTHDMPortSP(unit, config));
    SOC_IF_ERROR_RETURN(driverInitEgressTHDR(unit, config));
    SOC_IF_ERROR_RETURN(driverInitEgressTHDRConfig1(unit));
    SOC_IF_ERROR_RETURN(driverInitEgressOBM(unit, config));

    return BCM_E_NONE;
}

