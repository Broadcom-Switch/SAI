/*********************************************************************
 *
 * Copyright: (c) 2018 Broadcom.
 * Broadcom Proprietary and Confidential. All rights reserved.
 *
 *********************************************************************/

#include <stdlib.h>

#include <soc/property.h>
#include <sal/core/boot.h>
#include <sal/core/alloc.h>
#include <sal/appl/config.h>
#include <sal/appl/io.h>
#include <sal/appl/sal.h>
#include "appl/diag/shell.h"
#include <config.h>
#include <platform_config_api.h>

static char *platform_config_file_name = NULL;
static char *platform_config_prop_names[] = PLATFORM_SOC_PROP_NAMES_INITIALIZER;
static platform_config_methods_t platform_config_method = 
                                                    PLATFORM_CONFIG_DEFAULT;
unsigned int platform_flags = SAI_BOOT_F_DEFAULT;

static int platform_config_file_get(const char **fname);
static int platform_config_file_set(const char *fname);
static pc_t* platform_config_parse(char *str);
static int platform_config_get_lvalue(char *str, pc_t *pc);
static int platform_config_get_rvalue(char *str, pc_t *pc);
#ifdef CDP_PACKAGE
static int platform_config_search_valid_prop(char *name);
static int platform_config_prop_is_known(pc_t *pc);
#endif
static void platform_config_valid_prop_show(void);
void platform_config_show();

/*****************************************************************//**
 * \brief Return the config file name
 *
 * \param fname       [IN/OUT]  name of the config file
 *
 * \return 0                Success.
 * \return -1               Otherwise.
 *
 ********************************************************************/
static int platform_config_file_get(const char **fname)
{
  if(platform_config_file_name == NULL)
  {
    return -1;
  }

  *fname = platform_config_file_name;

  return 0;
}

/*****************************************************************//**
 * \brief Set the name of the active platform configuration file.
 *
 * \param fname       [IN]  name of config file
 *
 * \return 0                Success.
 *
 * \notes Makes a copy of the input string.
 *
 ********************************************************************/
static int platform_config_file_set(const char *fname)
{
  if (platform_config_file_name != NULL) {
    sal_free(platform_config_file_name);
    platform_config_file_name = NULL;
  }

  if (fname != NULL) {
    platform_config_file_name = sal_strdup(fname);
  }

  return 0;
}

/*****************************************************************//**
 * \brief Return the method used to take platform configuration
 *
 * \param mode        [IN/OUT]  To store platform config method
 *
 * \return  0         Success.
 * \return -1         Otherwise.
 *
 ********************************************************************/
int platform_config_method_get(int *mode)
{
  if(mode == NULL)
  {
    return -1;
  }

  *mode = platform_config_method;

  return 0;
}

/*****************************************************************//**
 * \brief Set the method used to take platform configuration
 *
 * \param mode       [IN]  Platform config method
 *
 * \return  0        Success.
 * \return -1        Otherwise.
 *
 ********************************************************************/
static int platform_config_method_set(int mode)
{
  if ((mode <= 0) || (mode >= PLATFORM_CONFIG_LAST)) {
    return -1;
  }

  platform_config_method = mode;

  return 0;
}

/*****************************************************************//**
 * \brief Identify the method used for platform customization
 *
 * \param init      [IN]    SAI driver initialization structure containing
 *                          config file path, boot flags, etc.
 *
 * \return -1               Illegal parameter
 * \return  0               Success
 *
 * \notes   Makes a copy of the input string.
 ********************************************************************/
int platform_config_init(sai_init_t *init)
{
  char *s = NULL;

#ifdef PLISIM
#define DEFAULT_BOOT_FLAGS (BOOT_F_PLISIM | BOOT_F_NO_PROBE)
#else
#define DEFAULT_BOOT_FLAGS 0
#endif

  platform_flags = DEFAULT_BOOT_FLAGS;
  sal_boot_flags_set(platform_flags);

  if ((s = getenv("SAI_BOOT_FLAGS")) != NULL) {
    platform_flags = sal_ctoi(s, NULL);
    platform_flags |= sal_boot_flags_get();
    sal_boot_flags_set(platform_flags);
  }

  /* Method 1 - Config file path is passed as an input parameter to
   *  the sai_driver_init() */
  if( (init != NULL))
  {
      if((platform_config_file_name == NULL) &&
         (init->cfg_fname != NULL))
    {
      platform_config_file_set(init->cfg_fname);
      platform_config_method_set(PLATFORM_CONFIG_INIT_PARAM);
    }

    /* Update boot flags */
    platform_flags = sal_boot_flags_get() | init->flags;
    sal_boot_flags_set(platform_flags);
  }

  /* Method 3 - Configuration file is not found. Use default configuration
   * embedded in the image itself. */
  if(platform_config_file_name != NULL)
  {
    printf("Platform configuration file \"%s\" is used\n",
        platform_config_file_name);
  }
  else
  {
    printf("Platform default configuration is used\n");
    platform_config_method_set(PLATFORM_CONFIG_DEFAULT);
  }
  printf("Platform Boot flags: 0x%0x\n", platform_flags);

  return 0;
}

/*****************************************************************//**
 * \brief To customize the platform using post init script file.
 *        The diag commands listed in this file are applied after
 *        the system is initialized.
 *
 * \param init      [IN]    SAI driver initialization structure containing
 *                          config file path, boot flags, etc.
 *
 * \return -1               Failure
 * \return  0               Success
 *
 * \notes   None
 ********************************************************************/
int platform_config_post_init(sai_init_t *init)
{
  char *config_file = NULL;
  char command[256];

  if((init != NULL && (SAI_F_FAST_BOOT & init->sai_flags))
#ifdef BCM_WARM_BOOT_SUPPORT
#if defined(BCM_ESW_SUPPORT)
      || (sal_boot_flags_get() & BOOT_F_WARM_BOOT)
#endif /* BCM_ESW_SUPPORT */
#endif /* BCM_WARM_BOOT_SUPPORT */
    )
  {
    /* Skip application of post init config if in fastboot/warmboot mode */
    return 0;
  }

  if((config_file == NULL) && (init != NULL))
  {
    if(init->cfg_post_fname != NULL)
    {
      config_file = init->cfg_post_fname;
    }
  }

  if(config_file != NULL)
  {
    if (snprintf(command, sizeof(command), "rcload %s", config_file) > 0)
    {
      if (sh_process_command(0, command) != 0)
      {
        return -1;
      }
      printf("Applied post initialization script from "
          "file \"%s\"\n", config_file);
    }
  }

  return 0;
}

/*****************************************************************//**
 * \brief Apply platform customization properties provided through config file
 *
 * \param init      [IN]    SAI driver initialization structure containing
 *                          config file path, boot flags, etc.
 *
 * \return  0               configuration applied successfully
 * \return -1               Illegal parameter
 *
 ********************************************************************/
int platform_config_apply(sai_init_t *init)
{
  pc_t *pc;
  FILE  *fp;
  char  str[PLATFORM_CONFIG_STR_MAX], *c;
  int line = 0;
  char *fname;

  /* scache filename set in config file takes precedence over this. */
  if((NULL != init) && (NULL != init->wb_fname))
  {
    sal_config_set("scache_filename", init->wb_fname);
  }

  fname = platform_config_file_name;

  if((fname == NULL) || (fname[0] == 0))
  {
    /* Nothing to apply */
    return 0;
  }


  /* Try to load config file ... */
  if ((fp = sal_fopen(fname, "r")) == NULL) {
    printf("Failed to open platform configuration file : %s\n", fname);
    return -1;
  }

  /* Read the entire file  - parsing as we go */

  while (sal_fgets(str, sizeof(str), fp)) {

    line++;

    /* Skip comment lines */
    if (str[0] == '#') {
      continue;
    }

    /* Strip trailing newline/blanks */
    c = str + strlen(str);
    while (c > str && isspace((unsigned) c[-1])) {
      c--;
    }

    *c = '\0';

    /* Skip blank lines */
    if (str[0] == 0) {
      continue;
    }

    if ((pc = platform_config_parse(str)) == NULL) {
      printf("Platform configuration: format error "
          "in %s on line %d (ignored)\n",
          fname, line);
      continue;
    }

#ifdef CDP_PACKAGE /* validate config property only in CDP */
    if (platform_config_prop_is_known(pc) == FALSE) {
      printf("Platform configuration: unknown entry \"%s\""
          " on %s line %d\n", pc->pc_name, fname, line);

      FREE_PC(pc);
      continue;
    }
#endif
    sal_config_set(pc->pc_name, pc->pc_value);

    FREE_PC(pc);

  } /* End of while (sal_fgets) */
  sal_fclose(fp);


  if((NULL != init) && (SAI_F_FAST_BOOT & init->sai_flags))
  {
    /* Skip firmware download as the boot method is fastboot */
    printf("Configuring switch to skip PHY firmware download\n");
    sal_config_set(spn_PHY_FORCE_FIRMWARE_LOAD, "1");
  }

  return  0;
}

/*****************************************************************//**
 * \brief Parse a single input line
 *
 * \param str       [IN]    pointer to null terminated input line.
 *
 * \return NULL             failed to parse.
 * \return !NULL            pointer to pc entry.
 *
 * \notes   Allocates memory for pc entry.
 *
 ********************************************************************/
static pc_t* platform_config_parse(char *str)
{
    pc_t    *pc;

    pc = (pc_t *)sal_alloc(sizeof(pc_t), "platform config parse");
    if (pc != NULL) {
        sal_memset(pc, 0, sizeof(pc_t));
    }

    if (!pc || !platform_config_get_lvalue(str, pc) ||
        !platform_config_get_rvalue(str, pc)) {
        return NULL;
    }

    return pc;
}

/*****************************************************************//**
 * \brief Update lvalue in the structure based on the input config string
 *
 * \param str       [IN]    pointer to null terminated input line.
 * \param pc        [IN]    pointer to config entry
 *
 * \return TRUE             Extracted 'Name' from the config string
 * \return FALSE            otherwise
 *
 ********************************************************************/
static int platform_config_get_lvalue(char *str, pc_t *pc)
{
    char *equals_loc;

    if ((equals_loc = strchr(str, '=')) == NULL) {
        if (pc != NULL) {
            FREE_PC(pc);
        }
        return FALSE;
    }

    while (isspace((unsigned)*str)) {
        str++;              /* skip leading whitespace */
    }

    if (str == equals_loc) {
        if (pc != NULL) {
            FREE_PC(pc);
        }
        return FALSE;           /* lvalue is empty or only whitespace */
    }

    while (isspace((unsigned)*(equals_loc - 1)) && equals_loc > str) {
        equals_loc--;       /* trim trailing whitespace */
    }

    pc->pc_name = sal_alloc(equals_loc - str + 1, "platform config name");
    if (pc->pc_name == NULL)
    {
      FREE_PC(pc);
      return FALSE;
    }
    sal_strncpy(pc->pc_name, str, equals_loc - str);

    pc->pc_name[equals_loc - str] = '\0';

    return TRUE;
}

/*****************************************************************//**
 * \brief Update rvalue in the structure based on the input config string
 *
 * \param str       [IN]    pointer to null terminated input line.
 * \param pc        [IN]    pointer to config entry
 *
 * \return TRUE             Extracted 'Value' from the config string
 * \return FALSE            otherwise
 *
 ********************************************************************/
static int platform_config_get_rvalue(char *str, pc_t *pc)
{
    char *begin;
    char *end;

    if ((begin = strchr(str, '=')) == NULL) {
        if (pc != NULL) {
            FREE_PC(pc);
        }
        return FALSE;
    }

    begin++;             /* Move past '=' */
    while (isspace((unsigned)*begin)) {
        begin++;         /* Trim leading whitespace */
    }

    for (end = begin + strlen(begin) - 1; isspace((unsigned)*end); end--)
        *end = '\0';     /* Trim trailing whitespace */

    pc->pc_value = sal_alloc(end - begin + 2, "platform config value");
    if (pc->pc_value == NULL) {
        FREE_PC(pc);
        return FALSE;
    }

    sal_strncpy(pc->pc_value, begin, end - begin + 1);
    pc->pc_value[end - begin + 1] = '\0';

    return TRUE;
}

#ifdef CDP_PACKAGE
/*****************************************************************//**
 * \brief Find out if the config property is allowed
 *
 * \param name      [IN]    pointer to property name.
 *
 * \return TRUE             config property is allowed
 * \return FALSE            otherwise
 *
 ********************************************************************/
static int platform_config_search_valid_prop(char *name)
{
    int i;

    for (i = 0; platform_config_prop_names[i][0] != '\0'; i++) {
        if (sal_strcasecmp(platform_config_prop_names[i], name) == 0) {
          return TRUE;
        }
    }

    return FALSE;
}

/*****************************************************************//**
 * \brief Determine if a given property is valid or not
 *
 * \param pc        [IN]    pointer to config entry
 *
 * \return TRUE             config property is allowed
 * \return FALSE            otherwise
 *
 ********************************************************************/
static int platform_config_prop_is_known(pc_t *pc)
{
    static char name[256];
    char *loc;

    sal_strncpy(name, pc->pc_name, sizeof(name) - 1);
    name[sizeof(name) - 1] = '\0';
    if (platform_config_search_valid_prop(name)) {
        return TRUE;
    }

    /* try removing the . if there is one, for "prop.0" style */
    loc = sal_strrchr(name, '.');
    if (loc != NULL) {
        *loc = '\0';
        if (platform_config_search_valid_prop(name)) {
            return TRUE;
        }
    }

    /* try removing the . again if there is another one,
       for "prop.port.0" style */
    loc = sal_strrchr(name, '.');
    if (loc != NULL) {
        *loc = '\0';
        if (platform_config_search_valid_prop(name)) {
            return TRUE;
        }
    }

    /* try removing the . again if there is another one,
       for "prop.prop1.port.0" style */
    loc = sal_strrchr(name, '.');
    if (loc != NULL) {
        *loc = '\0';
        if (platform_config_search_valid_prop(name)) {
            return TRUE;
        }
    }

    /* try removing the . again if there is another one,
       for "prop.prop1.prop2.port.0" style */
    loc = sal_strrchr(name, '.');
    if (loc != NULL) {
        *loc = '\0';
        if (platform_config_search_valid_prop(name)) {
            return TRUE;
        }
    }

    /* try removing the . again if there is another one,
       for "prop.prop1.prop2.prop3.port.0" style */
    loc = sal_strrchr(name, '.');
    if (loc != NULL) {
        *loc = '\0';
        if (platform_config_search_valid_prop(name)) {
            return TRUE;
        }
    }

    /* try removing the last brace if it exists, for "prop{0}" style */
    loc = sal_strrchr(name, '{');
    if (loc != NULL) {
        *loc = '\0';
        if (platform_config_search_valid_prop(name)) {
            return TRUE;
        }
    }

    /* try removing the last underscore if it exists, for "prop_xe.0" style */
    loc = sal_strrchr(name, '_');
    if (loc != NULL) {
        *loc = '\0';
        if (platform_config_search_valid_prop(name)) {
            return TRUE;
        }
    }

    /*
     * try removing one more underscore if it exists, for "prop_in_203.bcm88650"
     * style
     */
    loc = sal_strrchr(name, '_');
    if (loc != NULL) {
        *loc = '\0';
        if (platform_config_search_valid_prop(name)) {
            return TRUE;
        }
    }

    return FALSE;
}
#endif

/*****************************************************************//**
 * \brief Display allowed configuration types.
 *
 * \param void
 *
 * \return void
 *
 ********************************************************************/
static void platform_config_valid_prop_show(void)
{
  int i;

  for (i = 0; platform_config_prop_names[i][0] != '\0'; i++) {
    printf("%s\n", platform_config_prop_names[i]);
  }
  printf("\n");
}

/*****************************************************************//**
 * \brief Display platform configuration parameters
 *
 * \param void
 *
 * \return void
 *
 ********************************************************************/
void platform_config_show()
{
  const char *fname = NULL;
  platform_config_file_get(&fname);
  printf("platform config file   : '%s'\n", (fname == NULL) ? "" : fname);
  printf("platform config method : 0x%0x\n", platform_config_method);

  printf("Allowed config properties: \n");
  platform_config_valid_prop_show();
}
