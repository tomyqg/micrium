/***************************************************************************//**
 * @file
 * @brief Network Security Port Layer - Mocana NanoSSl
 *******************************************************************************
 * # License
 * <b>Copyright 2018 Silicon Laboratories Inc. www.silabs.com</b>
 *******************************************************************************
 *
 * The licensor of this software is Silicon Laboratories Inc.  Your use of this
 * software is governed by the terms of Silicon Labs Master Software License
 * Agreement (MSLA) available at
 * www.silabs.com/about-us/legal/master-software-license-agreement.
 * The software is governed by the sections of the MSLA applicable to Micrium
 * Software.
 *
 ******************************************************************************/

/********************************************************************************************************
 ********************************************************************************************************
 *                                       DEPENDENCIES & AVAIL CHECK(S)
 ********************************************************************************************************
 *******************************************************************************************************/

#include  <rtos_description.h>

#if (defined(RTOS_MODULE_NET_AVAIL) \
  && defined(RTOS_MODULE_NET_SSL_TLS_MOCANA_NANOSSL_AVAIL))

#if (!defined(RTOS_MODULE_COMMON_CLK_AVAIL))
#error Mocana nanoSSL module requires Clock module. Make sure it is part of your project \
  and that RTOS_MODULE_COMMON_CLK_AVAIL is defined in rtos_description.h.
#endif

/********************************************************************************************************
 ********************************************************************************************************
 *                                               INCLUDE FILES
 ********************************************************************************************************
 *******************************************************************************************************/

#include  <em_core.h>

#include  <net/include/net_cfg_net.h>
#include  <common/include/rtos_path.h>

#include  <net/include/net_secure.h>
#include  <net/source/tcpip/net_sock_priv.h>
#include  <net/source/ssl_tls/net_secure_priv.h>

#include  <common/include/rtos_err.h>
#include  <common/include/lib_str.h>
#include  <common/source/logging/logging_priv.h>

#include  <common/mtypes.h>
#include  <common/moptions.h>
#include  <common/mtypes.h>
#include  <common/mdefs.h>
#include  <common/merrors.h>
#include  <common/mrtos.h>
#include  <common/mtcp.h>
#include  <common/mocana.h>
#include  <common/debug_console.h>
#include  <common/mstdlib.h>
#include  <crypto/hw_accel.h>
#include  <crypto/ca_mgmt.h>
#include  <common/sizedbuffer.h>
#include  <crypto/cert_store.h>
#include  <ssl/ssl.h>

/********************************************************************************************************
 ********************************************************************************************************
 *                                               LOCAL DEFINES
 ********************************************************************************************************
 *******************************************************************************************************/

#define  LOG_DFLT_CH                     (NET, SSL)
#define  RTOS_MODULE_CUR                  RTOS_CFG_MODULE_NET

#define  NET_SECURE_MEM_BLK_TYPE_SSL_SESSION                 1u
#define  NET_SECURE_MEM_BLK_TYPE_SERVER_DESC                 2u
#define  NET_SECURE_MEM_BLK_TYPE_CLIENT_DESC                 3u

/********************************************************************************************************
 ********************************************************************************************************
 *                                           LOCAL DATA TYPES
 ********************************************************************************************************
 *******************************************************************************************************/

typedef  struct  net_secure_server_desc {
  certStorePtr   CertStorePtr;
  certDescriptor CertDesc;
} NET_SECURE_SERVER_DESC;

typedef  struct  net_secure_client_desc {
  CPU_CHAR                         *CommonNamePtr;
  NET_SOCK_SECURE_UNTRUSTED_REASON UntrustedReason;
  NET_SOCK_SECURE_TRUST_FNCT       TrustCallBackFnctPtr;
#ifdef __ENABLE_MOCANA_SSL_MUTUAL_AUTH_SUPPORT__
  certStorePtr                     CertStorePtr;
  certDescriptor                   CertDesc;
  CPU_INT08U                       *KeyPtr;
#endif
} NET_SECURE_CLIENT_DESC;

typedef  struct  net_secure_session {
  sbyte4               ConnInstance;
  NET_SOCK_SECURE_TYPE Type;
  void                 *DescPtr;
} NET_SECURE_SESSION;

//                                                                 ---------------- NET SECURE POOLS ------------------
typedef  struct  net_secure_mem_pools {
  MEM_DYN_POOL SessionPool;
  MEM_DYN_POOL ServerDescPool;
  MEM_DYN_POOL ClientDescPool;
} NET_SECURE_MEM_POOLS;

/********************************************************************************************************
 ********************************************************************************************************
 *                                       LOCAL GLOBAL VARIABLES
 ********************************************************************************************************
 *******************************************************************************************************/

static certDescriptor CaCertDesc;
static CPU_INT08U     CaBuf[NET_SECURE_CFG_MAX_CA_CERT_LEN];

static NET_SECURE_MEM_POOLS NetSecure_Pools;

/********************************************************************************************************
 ********************************************************************************************************
 *                                       LOCAL FUNCTION PROTOTYPES
 ********************************************************************************************************
 *******************************************************************************************************/

void NetSecure_MocanaFnctLog(sbyte4 module,
                             sbyte4 severity,
                             sbyte  *msg);

static sbyte4 NetSecure_CertificateStoreLookup(sbyte4                connectionInstance,
                                               certDistinguishedName *pLookupCertDN,
                                               certDescriptor        *pReturnCert);

static sbyte4 NetSecure_CertificateStoreVerify(sbyte4 connectionInstance,
                                               ubyte  *pCertificate,
                                               ubyte4 certificateLength,
                                               sbyte4 isSelfSigned);
#if 0
static CPU_BOOLEAN NetSecure_ExtractCertDN(CPU_CHAR              *p_buf,
                                           CPU_INT32U            buf_len,
                                           certDistinguishedName *p_dn);
#endif

static certDescriptor NetSecure_CertKeyConvert(const CPU_INT08U             *p_cert,
                                               CPU_SIZE_T                   cert_size,
                                               const CPU_INT08U             *p_key,
                                               CPU_SIZE_T                   key_size,
                                               NET_SOCK_SECURE_CERT_KEY_FMT fmt,
                                               RTOS_ERR                     *p_err);

/********************************************************************************************************
 ********************************************************************************************************
 *                                           PUBLIC FUNCTIONS
 ********************************************************************************************************
 *******************************************************************************************************/

/****************************************************************************************************//**
 *                                       NetSecure_CA_CertInstall()
 *
 * @brief    Install certificate autority's certificate.
 *
 * @param    p_ca_cert       Pointer to CA certificate.
 *
 * @param    ca_cert_len     Certificate length.
 *
 * @param    fmt             Certificate format:
 *                               - NET_SOCK_SECURE_CERT_KEY_FMT_PEM
 *                               - NET_SOCK_SECURE_CERT_KEY_FMT_DER
 *
 * @param    p_err           Pointer to variable that will receive the return error code from this function :
 *                               - RTOS_ERR_NONE
 *                               - RTOS_ERR_FAIL
 *                               - RTOS_ERR_INVALID_TYPE
 *
 * @return   DEF_OK,
 *           DEF_FAIL.
 *******************************************************************************************************/
CPU_BOOLEAN NetSecure_CA_CertInstall(const void                   *p_ca_cert,
                                     CPU_INT32U                   ca_cert_len,
                                     NET_SOCK_SECURE_CERT_KEY_FMT fmt,
                                     RTOS_ERR                     *p_err)
{
  CPU_BOOLEAN rtn_val;
  CPU_INT32S  rc;

  //                                                               ---------------- VALIDATE CERT LEN -----------------
  RTOS_ASSERT_DBG_ERR_SET((ca_cert_len <= NET_SECURE_CFG_MAX_CA_CERT_LEN), *p_err, RTOS_ERR_INVALID_ARG, DEF_FAIL);

  RTOS_ERR_SET(*p_err, RTOS_ERR_NONE);

  Net_GlobalLockAcquire((void *)&NetSecure_CA_CertInstall);

  Mem_Copy(CaBuf, p_ca_cert, ca_cert_len);

  rc = LAST_ERROR;

  switch (fmt) {
    case NET_SOCK_SECURE_CERT_KEY_FMT_PEM:
      rc = CA_MGMT_decodeCertificate(CaBuf, ca_cert_len, &CaCertDesc.pCertificate, &CaCertDesc.certLength);
      if (rc != OK) {
        RTOS_ERR_SET(*p_err, RTOS_ERR_FAIL);
        rtn_val = DEF_FAIL;
        goto exit;
      }
      break;

    case NET_SOCK_SECURE_CERT_KEY_FMT_DER:
      CaCertDesc.pCertificate = CaBuf;
      CaCertDesc.certLength = ca_cert_len;
      break;

    case NET_SOCK_SECURE_CERT_KEY_FMT_NONE:
    default:
      RTOS_ERR_SET(*p_err, RTOS_ERR_INVALID_TYPE);
      rtn_val = DEF_FAIL;
      goto exit;
  }

exit:
  Net_GlobalLockRelease();

  return (rtn_val);
}

/****************************************************************************************************//**
 *                                               NetSecure_Log()
 *
 * @brief    log the given string.
 *
 * @param    p_str   Pointer to string to log.
 *******************************************************************************************************/
void NetSecure_Log(CPU_CHAR *p_str)
{
  PP_UNUSED_PARAM(p_str);                                       // Prevent unused param warn when log level not reached
  LOG_VRB(((s)p_str));
}

/****************************************************************************************************//**
 *                                           NetSecure_ExtractCertDN()
 *
 * @brief    Extract certificate distinguished name into a string.
 *
 * @param    p_buf       Pointer to string fill.
 *
 * @param    buf_len     Buffer length.
 *
 * @param    p_dn        Pointer to distinguished name.
 *
 * @return   DEF_OK,   all distinguished name data printed successfully.
 *           DEF_FAIL, otherwise.
 *******************************************************************************************************/
#if 0
CPU_BOOLEAN NetSecure_ExtractCertDN(CPU_CHAR              *p_buf,
                                    CPU_INT32U            buf_len,
                                    certDistinguishedName *p_dn)
{
  CPU_CHAR    *p_str;
  relativeDN  *p_relative_dn;
  nameAttr    *p_name_attr;
  CPU_INT32U  dn_ctr;
  CPU_INT32U  item_ctr;
  CPU_INT32U  len;
  CPU_INT32U  rem_len;
  CPU_BOOLEAN wr_started;

  p_str = p_buf;
  rem_len = buf_len;
  wr_started = DEF_NO;

  p_relative_dn = p_dn->pDistinguishedName;

  for (dn_ctr = 0; dn_ctr < p_dn->dnCount; dn_ctr++) {
    p_name_attr = p_relative_dn->pNameAttr;
    for (item_ctr = 0; item_ctr < p_relative_dn->nameAttrCount; item_ctr++) {
      if (wr_started == DEF_YES) {
        len = DEF_MIN(rem_len, 4);
        Str_Copy_N(p_str, " - ", len);
        rem_len -= 4;
        p_str += 3;
      }

      if (p_name_attr->type == 19) {
        len = DEF_MIN(rem_len, p_name_attr->valueLen);
        if (len == 0) {
          return (DEF_FAIL);
        }
        Str_Copy_N(p_str, (CPU_CHAR *)p_name_attr->value, len);
        rem_len -= (p_name_attr->valueLen);
        p_str += (p_name_attr->valueLen);
        *p_str = ASCII_CHAR_NULL;
        if (wr_started != DEF_YES) {
          wr_started = DEF_YES;
        }
      }

      p_name_attr++;
    }

    p_relative_dn++;
  }

  return (DEF_OK);
}
#endif

/********************************************************************************************************
 ********************************************************************************************************
 *                                           GLOBAL FUNCTIONS
 ********************************************************************************************************
 *******************************************************************************************************/

/****************************************************************************************************//**
 *                                               NetSecure_Init()
 *
 * @brief    (1) Initialize security port :
 *               - (a) Initialize security memory pools
 *               - (b) Initialize CA descriptors
 *               - (c) Initialize Mocana
 *
 * @param    p_err   Pointer to variable that will receive the return error code from this function.
 *******************************************************************************************************/
void NetSecure_Init(MEM_SEG  *p_mem_seg,
                    RTOS_ERR *p_err)
{
  CPU_INT32S rc;
  CORE_DECLARE_IRQ_STATE;

  RTOS_ERR_SET(*p_err, RTOS_ERR_NONE);

  LOG_VRB(("SSL - ", (s)__FUNCTION__, ": Start"));

  Mem_DynPoolCreate("SSL Session pool",
                    &NetSecure_Pools.SessionPool,
                    p_mem_seg,
                    sizeof(NET_SECURE_SESSION),
                    sizeof(CPU_ALIGN),
                    0u,
                    LIB_MEM_BLK_QTY_UNLIMITED,
                    p_err);
  if (RTOS_ERR_CODE_GET(*p_err) != RTOS_ERR_NONE) {
    LOG_ERR(("SSL - ", (s)__FUNCTION__, "%: Mem_DynPoolCreate() returned: ", (s)RTOS_ERR_STR_GET(RTOS_ERR_CODE_GET(*p_err))));
    return;
  }

  Mem_DynPoolCreate("SSL Server Descriptor pool",
                    &NetSecure_Pools.ServerDescPool,
                    p_mem_seg,
                    sizeof(NET_SECURE_SERVER_DESC),
                    sizeof(CPU_ALIGN),
                    0u,
                    LIB_MEM_BLK_QTY_UNLIMITED,
                    p_err);
  if (RTOS_ERR_CODE_GET(*p_err) != RTOS_ERR_NONE) {
    LOG_ERR(("SSL - ", (s)__FUNCTION__, "%: Mem_DynPoolCreate() returned: ", (s)RTOS_ERR_STR_GET(RTOS_ERR_CODE_GET(*p_err))));
    return;
  }

  Mem_DynPoolCreate("SSL Client Descriptor pool",
                    &NetSecure_Pools.ClientDescPool,
                    p_mem_seg,
                    sizeof(NET_SECURE_SERVER_DESC),
                    sizeof(CPU_ALIGN),
                    0u,
                    LIB_MEM_BLK_QTY_UNLIMITED,
                    p_err);
  if (RTOS_ERR_CODE_GET(*p_err) != RTOS_ERR_NONE) {
    LOG_ERR(("SSL - ", (s)__FUNCTION__, "%: Mem_DynPoolCreate() returned: ", (s)RTOS_ERR_STR_GET(RTOS_ERR_CODE_GET(*p_err))));
    return;
  }

 #ifdef NET_SECURE_MODULE_EN
  //                                                               Init CA desc.
  CaCertDesc.pCertificate = NULL;
  CaCertDesc.certLength = 0;
#endif

  //                                                               Init Mocana nanoSSL.
  rc = -1;
  rc = MOCANA_initMocana();
  if (rc != OK) {
    RTOS_ERR_SET(*p_err, RTOS_ERR_INIT);
    return;
  }

  MOCANA_initLog(NetSecure_MocanaFnctLog);

  CORE_ENTER_ATOMIC();
  rc = SSL_init(NET_SECURE_CFG_MAX_NBR_SOCK_SERVER, NET_SECURE_CFG_MAX_NBR_SOCK_CLIENT);
  CORE_EXIT_ATOMIC();
  if (rc != OK) {
    LOG_ERR(("SSL - ", (s)__FUNCTION__, "%: SSL_init() returned: ", (s)(CPU_CHAR *)MERROR_lookUpErrorCode((MSTATUS)rc)));
    RTOS_ERR_SET(*p_err, RTOS_ERR_INIT);
    return;
  }

  SSL_sslSettings()->funcPtrCertificateStoreVerify = NetSecure_CertificateStoreVerify;
  SSL_sslSettings()->funcPtrCertificateStoreLookup = NetSecure_CertificateStoreLookup;

  LOG_VRB(("SSL - ", (s)__FUNCTION__, ": Normal exit"));
}

/****************************************************************************************************//**
 *                                           NetSecure_InitSession()
 *
 * @brief    Initialize a new secure session.
 *
 * @param    p_sock  Pointer to the accepted/connected socket.
 *
 * @param    p_err   Pointer to variable that will receive the return error code from this function.
 *******************************************************************************************************/
void NetSecure_InitSession(NET_SOCK *p_sock,
                           RTOS_ERR *p_err)
{
#ifdef   NET_SECURE_MODULE_EN
  NET_SECURE_SESSION *p_blk;

  //                                                               Get SSL session buf.
  p_blk = Mem_DynPoolBlkGet(&NetSecure_Pools.SessionPool, p_err);
  if (RTOS_ERR_CODE_GET(*p_err) != RTOS_ERR_NONE) {
    LOG_ERR(("SSL - ", (s)__FUNCTION__, ": Failed to acquire an SSL secure session"));
    return;
  }

  p_blk->ConnInstance = 0u;
  p_blk->DescPtr = DEF_NULL;
  p_blk->Type = NET_SOCK_SECURE_TYPE_NONE;
  p_sock->SecureSession = p_blk;

  RTOS_ERR_SET(*p_err, RTOS_ERR_NONE);
#else
  RTOS_ERR_SET(*p_err, RTOS_ERR_NOT_AVAIL);
#endif
}

/****************************************************************************************************//**
 *                                       NetSecure_SockCertKeyCfg()
 *
 * @brief    Configure server secure socket's certificate and key from buffers:
 *
 * @param    p_sock          Pointer to the server's socker to configure certificate and key.
 *
 * @param    sock_type       Secure socket type:
 *                               - NET_SOCK_SECURE_TYPE_SERVER
 *                               - NET_SOCK_SECURE_TYPE_CLIENT
 *
 * @param    p_buf_cert      Pointer to the certificate buffer to install.
 *
 * @param    buf_cert_size   Size of the certificate buffer to install.
 *
 * @param    p_buf_key       Pointer to the key buffer to install.
 *
 * @param    buf_key_size    Size of the key buffer to install.
 *
 * @param    fmt             Format of the certificate and key buffer to install.
 *                               - NET_SECURE_INSTALL_FMT_PEM      Certificate and Key format is PEM.
 *                               - NET_SECURE_INSTALL_FMT_DER      Certificate and Key format is DER.
 *
 * @param    cert_chain      Certificate point to a chain of certificate.
 *                               - DEF_YES     Certificate points to a chain  of certificate.
 *                               - DEF_NO      Certificate points to a single    certificate.
 *
 * @param    p_err           Pointer to variable that will receive the return error code from this function.
 *
 * @return   DEF_OK,   Server socket's certificate and key successfully configured.
 *           DEF_FAIL, otherwise.
 *******************************************************************************************************/

#if (NET_SECURE_CFG_MAX_NBR_SOCK_SERVER > 0u \
     || NET_SECURE_CFG_MAX_NBR_SOCK_CLIENT > 0u)
CPU_BOOLEAN NetSecure_SockCertKeyCfg(NET_SOCK                     *p_sock,
                                     NET_SOCK_SECURE_TYPE         sock_type,
                                     const CPU_INT08U             *p_buf_cert,
                                     CPU_SIZE_T                   buf_cert_size,
                                     const CPU_INT08U             *p_buf_key,
                                     CPU_SIZE_T                   buf_key_size,
                                     NET_SOCK_SECURE_CERT_KEY_FMT fmt,
                                     CPU_BOOLEAN                  cert_chain,
                                     RTOS_ERR                     *p_err)
{
  CPU_BOOLEAN rtn_val;
#ifdef   NET_SECURE_MODULE_EN
  CPU_INT32S             rc;
  NET_SECURE_SESSION     *p_session;
  NET_SECURE_SERVER_DESC *p_server_desc;
  NET_SECURE_CLIENT_DESC *p_client_desc;
  SizedBuffer            certificate;
  certStorePtr           *p_store;
  certDescriptor         cert_desc;

  PP_UNUSED_PARAM(cert_chain);
  LOG_VRB(("SSL - ", (s)__FUNCTION__, ": Start"));

  //                                                               ------------------- VALIDATE ARGS ------------------
  RTOS_ASSERT_DBG_ERR_SET((buf_cert_size <= NET_SECURE_CFG_MAX_CERT_LEN), *p_err, RTOS_ERR_INVALID_ARG, DEF_FAIL);
  RTOS_ASSERT_DBG_ERR_SET((buf_key_size <= NET_SECURE_CFG_MAX_KEY_LEN), *p_err, RTOS_ERR_INVALID_ARG, DEF_FAIL);

  p_session = (NET_SECURE_SESSION *)p_sock->SecureSession;
  RTOS_ASSERT_DBG_ERR_SET((p_session != DEF_NULL), *p_err, RTOS_ERR_NULL_PTR, DEF_FAIL);

  cert_desc = NetSecure_CertKeyConvert(p_buf_cert, buf_cert_size, p_buf_key, buf_key_size, fmt, p_err);
  if (RTOS_ERR_CODE_GET(*p_err) != RTOS_ERR_NONE) {
    return (DEF_FAIL);
  }

  switch (sock_type) {
    case NET_SOCK_SECURE_TYPE_SERVER:
      if (p_session->DescPtr) {
        RTOS_ERR_SET(*p_err, RTOS_ERR_INVALID_STATE);
        LOG_ERR(("SSL - ", (s)__FUNCTION__, " : ERROR multiple call to NetSecure_SockCertKeyCfg is not supported. The socket must be closed between each call"));
        return (DEF_FAIL);
      }

      p_server_desc = Mem_DynPoolBlkGet(&NetSecure_Pools.ServerDescPool, p_err);
      if (RTOS_ERR_CODE_GET(*p_err) != RTOS_ERR_NONE) {
        return (DEF_FAIL);
      }

      p_session->Type = NET_SOCK_SECURE_TYPE_SERVER;
      p_session->DescPtr = p_server_desc;
      p_store = &p_server_desc->CertStorePtr;
      p_server_desc->CertDesc = cert_desc;
      break;

    case NET_SOCK_SECURE_TYPE_CLIENT:
#ifdef __ENABLE_MOCANA_SSL_MUTUAL_AUTH_SUPPORT__

      p_client_desc = p_session->DescPtr;
      if (p_client_desc == DEF_NULL) {
        p_client_desc = Mem_DynPoolBlkGet(&NetSecure_Pools.ClientDescPool, p_err);
        if (RTOS_ERR_CODE_GET(*p_err) != RTOS_ERR_NONE) {
          return (DEF_FAIL);
        }
      }

      p_session->Type = NET_SOCK_SECURE_TYPE_CLIENT;
      p_store = &p_client_desc->CertStorePtr;
      break;
#else
      RTOS_ERR_SET(*p_err, RTOS_ERR_NOT_AVAIL);
      LOG_ERR(("SSL - ", (s)__FUNCTION__, " : Mutual authentication is not enabled, Add #define __ENABLE_MOCANA_SSL_MUTUAL_AUTH_SUPPORT__ in moption_custom.h"));
      return (DEF_FAIL);
#endif

    default:
      RTOS_ERR_SET(*p_err, RTOS_ERR_INVALID_ARG);
      return (DEF_FAIL);
  }

  rc = CERT_STORE_createStore(p_store);
  if (rc != OK) {
    LOG_ERR(("SSL - ", (s)__FUNCTION__, " : CERT_STORE_createStore() returned: ", (s)(CPU_CHAR *)MERROR_lookUpErrorCode((MSTATUS)rc)));
    RTOS_ERR_SET(*p_err, RTOS_ERR_FAIL);
    return (DEF_FAIL);
  }

  certificate.length = cert_desc.certLength;
  certificate.data = cert_desc.pCertificate;
  rc = CERT_STORE_addIdentityWithCertificateChain(*p_store, &certificate, 1, cert_desc.pKeyBlob, cert_desc.keyBlobLength);
  if (rc != OK) {
    LOG_ERR(("SSL - ", (s)__FUNCTION__, " : CERT_STORE_addIdentity() returned: ", (s)(CPU_CHAR *)MERROR_lookUpErrorCode((MSTATUS)rc)));
    RTOS_ERR_SET(*p_err, RTOS_ERR_FAIL);
    rtn_val = DEF_FAIL;
  }

  LOG_VRB(("SSL - ", (s)__FUNCTION__, ": Normal exit"));

#else
  rtn_val = DEF_FAIL;
  RTOS_ERR_SET(*p_err, RTOS_ERR_NOT_AVAIL);
#endif

  return (rtn_val);
}
#endif

/****************************************************************************************************//**
 *                                   NetSecure_SockServerCertKeyFiles()
 *
 * @brief    Configure server secure socket's certificate and key from buffers:
 *
 * @param    p_sock          Pointer to the server's socket to configure certificate and key.
 *
 * @param    p_buf_cert      Pointer to the certificate buffer to install.
 *
 * @param    buf_cert_size   Size of the certificate buffer to install.
 *
 * @param    p_buf_key       Pointer to the key buffer to install.
 *
 * @param    buf_key_size    Size of the key buffer to install.
 *
 * @param    fmt             Format of the certificate and key buffer to install.
 *                               - NET_SECURE_INSTALL_FMT_PEM      Certificate and Key format is PEM.
 *                               - NET_SECURE_INSTALL_FMT_DER      Certificate and Key format is DER.
 *
 * @param    cert_chain      Certificate point to a chain of certificate.
 *                               - DEF_YES     Certificate points to a chain  of certificate.
 *                               - DEF_NO      Certificate points to a single certificate.
 *
 * @param    p_err           Pointer to variable that will receive the return error code from this function.
 *
 * @return   DEF_OK,   Server socket's certificate and key successfully configured.
 *           DEF_FAIL, otherwise.
 *******************************************************************************************************/

#if 0
#if ((NET_SECURE_CFG_MAX_NBR_SOCK_SERVER > 0u) \
  && (NET_SECURE_CFG_FS_EN == DEF_ENABLED))
CPU_BOOLEAN NetSecure_SockServerCertKeyFiles(NET_SOCK                     *p_sock,
                                             const void                   *p_filename_cert,
                                             const void                   *p_filename_key,
                                             NET_SOCK_SECURE_CERT_KEY_FMT fmt,
                                             CPU_BOOLEAN                  cert_chain,
                                             RTOS_ERR                     *p_err)
{
  CPU_INT32S             rc;
  NET_SECURE_SESSION     *p_session;
  NET_SECURE_SERVER_DESC *p_server_desc;
  ubyte                  *p_buf_cert;
  ubyte                  *p_buf_key;
  CPU_INT32U             buf_cert_len;
  CPU_INT32U             buf_key_len;
  CPU_BOOLEAN            rtn_val;

  p_session = (NET_SECURE_SESSION *)p_sock->SecureSession;
  if (p_session == DEF_NULL) {
    RTOS_ERR_SET(*p_err, RTOS_ERR_NULL_PTR);
    return (DEF_FAIL);
  }

  switch (p_session->Type) {
    case NET_SOCK_SECURE_TYPE_NONE:
    case NET_SOCK_SECURE_TYPE_SERVER:
      break;

    case NET_SOCK_SECURE_TYPE_CLIENT:
    case NET_SOCK_SECURE_TYPE_ACCEPT:
    default:
      RTOS_ERR_SET(*p_err, RTOS_ERR_FAIL);
      return (DEF_FAIL);
  }

  LOG_VRB(("SSL - ", (s)__FUNCTION__, ": Start"));

  rc = MOCANA_readFile((sbyte const *)p_filename_cert, &p_buf_cert, &buf_cert_len);
  if (rc != OK) {
    LOG_ERR(("SSL - ", (s)__FUNCTION__, " : MOCANA_readFile() returned: ", (s)MERROR_lookUpErrorCode((MSTATUS)rc)));
    return (DEF_FAIL);
  }

  rc = MOCANA_readFile((sbyte const *)p_filename_key, &p_buf_key, &buf_key_len);
  if (rc != OK) {
    MOCANA_freeReadFile(&p_buf_cert);
    LOG_ERR(("SSL - ", (s)__FUNCTION__, " : MOCANA_readFile() returned: ", (s)MERROR_lookUpErrorCode((MSTATUS)rc)));
    return (DEF_FAIL);
  }

  rtn_val = NetSecure_SockCertKeyCfg(p_sock,
                                     p_buf_cert,
                                     buf_cert_len,
                                     p_buf_key,
                                     buf_key_len,
                                     fmt,
                                     cert_chain,
                                     p_err);

  MOCANA_freeReadFile(&p_buf_cert);
  MOCANA_freeReadFile(&p_buf_key);

  return (rtn_val);
}
#endif
#endif

/****************************************************************************************************//**
 *                                       NetSecure_ClientCommonNameSet()
 *
 * @brief    Configure client secure socket's Common name.
 *
 * @param    p_sock          Pointer to the client's socket to configure common name.
 *
 * @param    p_common_name   Pointer to the common name.
 *
 * @param    p_err           Pointer to variable that will receive the return error code from this function.
 *
 * @return   DEF_OK,   Client socket's common name successfully configured.
 *           DEF_FAIL, otherwise.
 *
 * @note
 *******************************************************************************************************/
#if (NET_SECURE_CFG_MAX_NBR_SOCK_CLIENT > 0)
CPU_BOOLEAN NetSecure_ClientCommonNameSet(NET_SOCK *p_sock,
                                          CPU_CHAR *p_common_name,
                                          RTOS_ERR *p_err)
{
#ifdef   NET_SECURE_MODULE_EN
  NET_SECURE_SESSION     *p_session;
  NET_SECURE_CLIENT_DESC *p_desc_client = DEF_NULL;

  RTOS_ERR_SET(*p_err, RTOS_ERR_NONE);

  p_session = (NET_SECURE_SESSION *)p_sock->SecureSession;
  RTOS_ASSERT_DBG_ERR_SET((p_session != DEF_NULL), *p_err, RTOS_ERR_NULL_PTR, DEF_FAIL);

  switch (p_session->Type) {
    case NET_SOCK_SECURE_TYPE_CLIENT:
    case NET_SOCK_SECURE_TYPE_NONE:
      p_desc_client = p_session->DescPtr;
      if (p_desc_client == DEF_NULL) {
        p_desc_client = Mem_DynPoolBlkGet(&NetSecure_Pools.ClientDescPool, p_err);
        if (RTOS_ERR_CODE_GET(*p_err) != RTOS_ERR_NONE) {
          return (DEF_FAIL);
        }
      }

      p_session->DescPtr = p_desc_client;
      p_session->Type = NET_SOCK_SECURE_TYPE_CLIENT;
      break;

    case NET_SOCK_SECURE_TYPE_SERVER:
    default:
      RTOS_DBG_FAIL_EXEC_ERR(*p_err, RTOS_ERR_INVALID_ARG, DEF_FAIL);
  }

  p_desc_client->CommonNamePtr = p_common_name;

  return (DEF_OK);

#else
  RTOS_ERR_SET(*p_err, RTOS_ERR_NOT_AVAIL);
  return (DEF_FAIL);
#endif
}
#endif

/****************************************************************************************************//**
 *                                   NetSecure_ClientTrustCallBackSet()
 *
 * @brief    Configure client secure socket's trust callback function.
 *
 * @param    p_sock              Pointer to the client's socket to configure trust call back function.
 *
 * @param    p_callback_fnct     Pointer to the trust call back function
 *
 * @param    p_err               Pointer to variable that will receive the return error code from this function.
 *
 * @return   DEF_OK,   Client socket's trust call back function successfully configured.
 *           DEF_FAIL, otherwise.
 *******************************************************************************************************/
#if (NET_SECURE_CFG_MAX_NBR_SOCK_CLIENT > 0)
CPU_BOOLEAN NetSecure_ClientTrustCallBackSet(NET_SOCK                   *p_sock,
                                             NET_SOCK_SECURE_TRUST_FNCT p_callback_fnct,
                                             RTOS_ERR                   *p_err)
{
#ifdef   NET_SECURE_MODULE_EN
  NET_SECURE_SESSION     *p_session;
  NET_SECURE_CLIENT_DESC *p_desc_client = DEF_NULL;

  RTOS_ERR_SET(*p_err, RTOS_ERR_NONE);

  p_session = (NET_SECURE_SESSION *)p_sock->SecureSession;
  RTOS_ASSERT_DBG_ERR_SET((p_session != DEF_NULL), *p_err, RTOS_ERR_NULL_PTR, DEF_FAIL);

  switch (p_session->Type) {
    case NET_SOCK_SECURE_TYPE_CLIENT:
    case NET_SOCK_SECURE_TYPE_NONE:
      p_desc_client = p_session->DescPtr;
      if (p_desc_client == DEF_NULL) {
        p_desc_client = Mem_DynPoolBlkGet(&NetSecure_Pools.ClientDescPool, p_err);
        if (RTOS_ERR_CODE_GET(*p_err) != RTOS_ERR_NONE) {
          return (DEF_FAIL);
        }
      }

      p_session->Type = NET_SOCK_SECURE_TYPE_CLIENT;
      break;

    case NET_SOCK_SECURE_TYPE_SERVER:
    default:
      RTOS_DBG_FAIL_EXEC_ERR(*p_err, RTOS_ERR_INVALID_ARG, DEF_FAIL);
  }

  p_desc_client->TrustCallBackFnctPtr = p_callback_fnct;

  return (DEF_OK);

#else
  RTOS_ERR_SET(*p_err, RTOS_ERR_NOT_AVAIL);
  return (DEF_FAIL);
#endif
}
#endif

/****************************************************************************************************//**
 *                                           NetSecure_SockConn()
 *
 * @brief    (1) Connect a socket to a remote host through an encryted SSL handshake :
 *               - (a) Get & validate the SSL session of the connected socket
 *               - (b) Initialize the     SSL connect.
 *               - (c) Perform            SSL handshake.
 *
 * @param    p_sock  Pointer to a connected socket.
 *
 *
 * Argument(s) : p_sock      Pointer to a connected socket.
 *
 *               p_err       Pointer to variable that will receive the return error code from this function.
 *
 * Return(s)   : none.
 *
 * Note(s)     : none.
 *******************************************************************************************************/
void NetSecure_SockConn(NET_SOCK *p_sock,
                        RTOS_ERR *p_err)
{
#ifdef   NET_SECURE_MODULE_EN
#ifdef __ENABLE_MOCANA_SSL_CLIENT__
  CPU_INT32S             rc;
  NET_SECURE_SESSION     *p_session;
  NET_SECURE_CLIENT_DESC *p_client_desc;
  const sbyte            *p_common_name;

  LOG_VRB(("SSL - ", (s)__FUNCTION__, ": Start"));

  //                                                               Get & validate SSL session of the connected sock.
  p_session = (NET_SECURE_SESSION *)p_sock->SecureSession;
  if (p_session->Type == NET_SOCK_SECURE_TYPE_CLIENT) {
    p_client_desc = (NET_SECURE_CLIENT_DESC *)p_session->DescPtr;
  } else {
    RTOS_ERR_SET(*p_err, RTOS_ERR_INVALID_TYPE);
    goto exit;
  }

  if (p_client_desc != DEF_NULL) {
    p_common_name = (const sbyte *)p_client_desc->CommonNamePtr;
  } else {
    p_common_name = (const sbyte *)DEF_NULL;
  }

  p_session->Type = NET_SOCK_SECURE_TYPE_CLIENT;

  //                                                               Init SSL connect.
  //                                                               Save the whole NET_SOCK because some NetOS ...
  //                                                               ... functions require it.
  Net_GlobalLockRelease();
  p_session->ConnInstance = SSL_connect(p_sock->ID,
                                        0,
                                        NULL,
                                        NULL,
                                        p_common_name);
  Net_GlobalLockAcquire((void *)&NetSecure_SockConn);
  if (p_session->ConnInstance < 0) {
    LOG_ERR(("SSL - ", (s)__FUNCTION__, " : SSL_Connect() returned: ", (s)(CPU_CHAR *)MERROR_lookUpErrorCode((MSTATUS)p_session->ConnInstance)));
    RTOS_ERR_SET(*p_err, RTOS_ERR_FAIL);
    goto exit;
  }

#ifdef  __ENABLE_MOCANA_SSL_MUTUAL_AUTH_SUPPORT__
  if (p_client_desc->CertStorePtr != DEF_NULL) {
    SSL_assignCertificateStore(p_session->ConnInstance, p_client_desc->CertStorePtr);
  }
#endif

  //                                                               Perform SSL handshake.
  rc = -1;
  Net_GlobalLockRelease();
  rc = SSL_negotiateConnection(p_session->ConnInstance);
  Net_GlobalLockAcquire((void *)&NetSecure_SockConn);
  if (rc != OK) {
    LOG_ERR(("SSL - ", (s)__FUNCTION__, " : SSL_negotiateConnection() returned: ", (s)(CPU_CHAR *)MERROR_lookUpErrorCode((MSTATUS)rc)));
    RTOS_ERR_SET(*p_err, RTOS_ERR_FAIL);
    goto exit;
  }

  LOG_VRB(("SSL - ", (s)__FUNCTION__, ": Normal exit"));

  RTOS_ERR_SET(*p_err, RTOS_ERR_NONE);
  goto exit;

#else
  RTOS_ERR_SET(*p_err, RTOS_ERR_NET_HANDSHAKE);                 // Mocana code not compiled with SSL client support.
  goto exit;
#endif
#else
  RTOS_ERR_SET(*p_err, RTOS_ERR_NOT_AVAIL);
  goto exit;
#endif

exit:
  return;
}

/****************************************************************************************************//**
 *                                           NetSecure_SockAccept()
 *
 * @brief    (1) Return a new secure socket accepted from a listen socket :
 *               - (a) Get & validate SSL session of listening socket
 *               - (b) Initialize     SSL session of accepted  socket
 *               - (c) Initialize     SSL accept
 *               - (d) Perform        SSL handshake
 *
 * @param    p_sock_listen   Pointer to a listening socket.
 *
 * @param    p_sock_accept   Pointer to an accepted socket.
 *
 * @param    p_err           Pointer to variable that will receive the return error code from this function.
 *
 * @note     (2) The SSL session of the listening socket has already been validated.  The session
 *               pointer of the accepted socket is also assumed to be valid.
 *
 * @note     (3) The listening SSL session is not initialized with the context information.  Then,
 *               the quiet shutdown option SHOULD be set to avoid trying to send encrypted data on
 *               the listening session.
 *******************************************************************************************************/
void NetSecure_SockAccept(NET_SOCK *p_sock_listen,
                          NET_SOCK *p_sock_accept,
                          RTOS_ERR *p_err)

{
#ifdef   NET_SECURE_MODULE_EN
#ifdef __ENABLE_MOCANA_SSL_SERVER__
  CPU_INT32S             rc;
  NET_SECURE_SESSION     *p_session_listen;
  NET_SECURE_SESSION     *p_session_accept;
  NET_SECURE_SERVER_DESC *p_server_desc;

  rc = -1;
  LOG_VRB(("SSL - ", (s)__FUNCTION__, ": Start"));

  //                                                               Get & validate SSL session of listening sock.
  p_session_listen = (NET_SECURE_SESSION *)p_sock_listen->SecureSession;
  if (p_session_listen->Type != NET_SOCK_SECURE_TYPE_SERVER) {
    RTOS_ERR_SET(*p_err, RTOS_ERR_INVALID_TYPE);
    goto exit;
  }

  p_server_desc = (NET_SECURE_SERVER_DESC *)p_session_listen->DescPtr;
  if (p_server_desc == DEF_NULL) {
    RTOS_ERR_SET(*p_err, RTOS_ERR_INVALID_STATE);
    goto exit;
  }

  //                                                               Initialize SSL session of accepted sock.
  NetSecure_InitSession(p_sock_accept, p_err);
  if (RTOS_ERR_CODE_GET(*p_err) != RTOS_ERR_NONE) {
    LOG_ERR(("SSL - ", (s)__FUNCTION__, " : Error: NO session available"));
    goto exit;
  }

  p_session_accept = (NET_SECURE_SESSION *)p_sock_accept->SecureSession;
  p_session_accept->Type = NET_SOCK_SECURE_TYPE_SERVER;

  //                                                               Init SSL accept.
  //                                                               Save the whole NET_SOCK because some NetOS ...
  //                                                               ... functions require it.
  p_session_accept->ConnInstance = SSL_acceptConnection((sbyte4)p_sock_accept->ID);
  if (p_session_accept->ConnInstance < 0) {
    LOG_ERR(("SSL - ", (s)__FUNCTION__, " : SSL_acceptConnection() Error, connection ID is lower than 0"));
    RTOS_ERR_SET(*p_err, RTOS_ERR_FAIL);
    goto exit;
  } else {
    LOG_DBG(("SSL - ", (s)__FUNCTION__, ": SSL_acceptConnection() accepted connection: ConnInstance = ", (u)p_session_accept->ConnInstance));
  }

  rc = SSL_assignCertificateStore(p_session_accept->ConnInstance, p_server_desc->CertStorePtr);
  if (rc != OK) {
    LOG_ERR(("SSL - ", (s)__FUNCTION__, " : SSL_assignCertificateStore() returned: ", (s)(CPU_CHAR *)MERROR_lookUpErrorCode((MSTATUS)rc)));
    RTOS_ERR_SET(*p_err, RTOS_ERR_FAIL);
    goto exit;
  }

#ifdef __ENABLE_MOCANA_SSL_MUTUAL_AUTH_SUPPORT__
  rc = SSL_setSessionFlags(p_session_accept->ConnInstance, SSL_FLAG_NO_MUTUAL_AUTH_REQUEST);
  if (rc != OK) {
    LOG_ERR(("SSL - ", (s)__FUNCTION__, " : SSL_ioctl() returned: ", (s)(CPU_CHAR *)MERROR_lookUpErrorCode((MSTATUS)rc)));
    goto exit;
  }
#endif

  Net_GlobalLockRelease();
  //                                                               Perform SSL handshake.
  rc = SSL_negotiateConnection(p_session_accept->ConnInstance);
  Net_GlobalLockAcquire((void *)&NetSecure_SockAccept);
  if (rc != OK) {
    NetSecure_SockClose(p_sock_accept, p_err);
    LOG_ERR(("SSL - ", (s)__FUNCTION__, " : SSL_negotiateConnection() returned: ", (s)(CPU_CHAR *)MERROR_lookUpErrorCode((MSTATUS)rc)));
    RTOS_ERR_SET(*p_err, RTOS_ERR_FAIL);
    goto exit;
  }

  RTOS_ERR_SET(*p_err, RTOS_ERR_NONE);
  LOG_VRB(("SSL - ", (s)__FUNCTION__, ": Normal exit"));
  goto exit;

exit:
  return;

#else

  RTOS_ERR_SET(*p_err, RTOS_ERR_NOT_AVAIL);                     // Mocana code not compiled with SSL server support.

#endif
#else
  RTOS_ERR_SET(*p_err, RTOS_ERR_NOT_AVAIL);
#endif
}

/****************************************************************************************************//**
 *                                       NetSecure_SockRxDataHandler()
 *
 * @brief    Receive clear data through a secure socket :
 *               - (a) Get & validate the SSL session of the receiving socket
 *               - (b) Receive the data
 *
 * @param    p_sock          Pointer to a receive socket.
 *
 * @param    p_data_buf      Pointer to an application data buffer that will receive the socket's
 *                           received data.
 *
 * @param    data_buf_len    Size of the application data buffer (in octets).
 *
 * @param    p_err           Pointer to variable that will receive the return error code from this function.
 *
 * @return   Number of positive data octets received, if NO error(s).
 *           NET_SOCK_BSD_ERR_RX, otherwise.
 *******************************************************************************************************/
NET_SOCK_RTN_CODE NetSecure_SockRxDataHandler(NET_SOCK   *p_sock,
                                              void       *p_data_buf,
                                              CPU_INT16U data_buf_len,
                                              RTOS_ERR   *p_err)
{
#ifdef   NET_SECURE_MODULE_EN
  NET_SECURE_SESSION *p_session;
  CPU_INT32S         rxd;
  CPU_INT32S         rc;

  rxd = 0;
  rc = -1;

  LOG_VRB(("SSL - ", (s)__FUNCTION__, ": Start"));

  p_session = (NET_SECURE_SESSION *)p_sock->SecureSession;

  Net_GlobalLockRelease();
  rc = SSL_recv(p_session->ConnInstance,
                p_data_buf,
                data_buf_len,
                (sbyte4 *)&rxd,
                0);
  Net_GlobalLockAcquire((void *)&NetSecure_SockRxDataHandler);
  if (OK != rc) {
    RTOS_ERR_CODE err = (RTOS_ERR_CODE)rc;

    if (ERR_TCP_SOCKET_CLOSED == rc) {
      RTOS_ERR_SET(*p_err, RTOS_ERR_NET_SOCK_CLOSED);
      return (NET_SOCK_BSD_RTN_CODE_CONN_CLOSED);
    } else if (err == RTOS_ERR_NET_IF_LINK_DOWN) {
      LOG_ERR(("SSL - ", (s)__FUNCTION__, " : SSL_recv() returned: ", (s)(CPU_CHAR *)MERROR_lookUpErrorCode((MSTATUS)rc)));
      RTOS_ERR_SET(*p_err, RTOS_ERR_NET_IF_LINK_DOWN);
    } else {
      LOG_ERR(("SSL - ", (s)__FUNCTION__, " : SSL_recv() returned: ", (s)(CPU_CHAR *)MERROR_lookUpErrorCode((MSTATUS)rc)));
      RTOS_ERR_SET(*p_err, RTOS_ERR_TX);
    }
  }

  RTOS_ERR_SET(*p_err, RTOS_ERR_NONE);

  LOG_VRB(("SSL - ", (s)__FUNCTION__, ": Normal exit"));

  return (rxd);                                                 // if successful return the number of bytes read.

#else
  RTOS_ERR_SET(*p_err, RTOS_ERR_NOT_AVAIL);
  return (0u);
#endif
}

/****************************************************************************************************//**
 *                                       NetSecure_SockRxIsDataPending()
 *
 * @brief    Is data pending in SSL receive queue.
 *
 * @param    p_sock  Pointer to a receive socket.
 *
 * @param    p_err   Pointer to variable that will receive the return error code from this function.
 *
 * @return   DEF_YES, If data is pending.
 *           DEF_NO,  Otherwise
 *******************************************************************************************************/
#if (NET_SOCK_CFG_SEL_EN == DEF_ENABLED)
CPU_BOOLEAN NetSecure_SockRxIsDataPending(NET_SOCK *p_sock,
                                          RTOS_ERR *p_err)
{
  sbyte4             pending = DEF_NO;
  sbyte4             status;
  NET_SECURE_SESSION *p_session;

  p_session = (NET_SECURE_SESSION *)p_sock->SecureSession;
  if (p_session == DEF_NULL) {
    RTOS_ERR_SET(*p_err, RTOS_ERR_NULL_PTR);
    goto exit;
  }

  status = SSL_recvPending(p_session->ConnInstance, &pending);
  if (status != OK) {
    RTOS_ERR_SET(*p_err, RTOS_ERR_INVALID_STATE);
    goto exit;
  }

  RTOS_ERR_SET(*p_err, RTOS_ERR_NONE);

exit:
  return ((CPU_BOOLEAN)pending);
}
#endif

/****************************************************************************************************//**
 *                                       NetSecure_SockTxDataHandler()
 *
 * @brief    Transmit clear data through a secure socket :
 *               - (a) Get & validate the SSL session of the transmitting socket
 *               - (b) Transmit the data
 *
 * @param    p_sock          Pointer to a transmit socket.
 *
 * @param    p_data_buf      Pointer to application data to transmit.
 *
 * @param    data_buf_len    Length of  application data to transmit (in octets).
 *
 * @param    p_err           Pointer to variable that will receive the return error code from this function.
 *
 * @return   Number of positive data octets transmitted, if NO error(s).
 *           NET_SOCK_BSD_ERR_RX, otherwise.
 *******************************************************************************************************/
NET_SOCK_RTN_CODE NetSecure_SockTxDataHandler(NET_SOCK   *p_sock,
                                              void       *p_data_buf,
                                              CPU_INT16U data_buf_len,
                                              RTOS_ERR   *p_err)
{
#ifdef   NET_SECURE_MODULE_EN
  NET_SECURE_SESSION *p_session;
  sbyte4             rc = -1;

  LOG_VRB(("SSL - ", (s)__FUNCTION__, ": Start"));

  p_session = (NET_SECURE_SESSION *)p_sock->SecureSession;
  Net_GlobalLockRelease();
  rc = SSL_send(p_session->ConnInstance, p_data_buf, data_buf_len);
  Net_GlobalLockAcquire((void *)&NetSecure_SockTxDataHandler);
  if (0 > rc) {
    RTOS_ERR_CODE err = (RTOS_ERR_CODE)(rc * -1);

    if (err == RTOS_ERR_NET_IF_LINK_DOWN) {
      LOG_ERR(("NanoSSL - ", (s)__FUNCTION__, " : NetSock_TxDataHandlerStream() error returned: ", (s)RTOS_ERR_STR_GET(err)));
      RTOS_ERR_SET(*p_err, RTOS_ERR_NET_IF_LINK_DOWN);
    } else {
      LOG_ERR(("SSL - ", (s)__FUNCTION__, " : SSL_send() returned: ", (s)(CPU_CHAR *)MERROR_lookUpErrorCode((MSTATUS)rc)));
      RTOS_ERR_SET(*p_err, RTOS_ERR_TX);
    }

    return NET_SOCK_BSD_ERR_RX;
  }

  RTOS_ERR_SET(*p_err, RTOS_ERR_NONE);

  LOG_VRB(("SSL - ", (s)__FUNCTION__, ": Normal exit"));

  return rc;

#else
  RTOS_ERR_SET(*p_err, RTOS_ERR_NOT_AVAIL);
  return (-1);
#endif
}

/****************************************************************************************************//**
 *                                           NetSecure_SockClose()
 *
 * @brief    (1) Close the secure socket :
 *               - (a) Get & validate the SSL session of the socket to close
 *               - (b) Transmit close notify alert to the peer
 *               - (c) Free the SSL session buffer
 *
 * @param    p_sock  Pointer to a socket.
 *
 * @param    p_err   Pointer to variable that will receive the return error code from this function.
 *******************************************************************************************************/
void NetSecure_SockClose(NET_SOCK *p_sock,
                         RTOS_ERR *p_err)
{
#ifdef   NET_SECURE_MODULE_EN
  NET_SECURE_SESSION     *p_session;
  NET_SECURE_SERVER_DESC *p_server_desc;
  NET_SECURE_CLIENT_DESC *p_client_desc;
  RTOS_ERR               err;

  LOG_VRB(("SSL - ", (s)__FUNCTION__, ": Start"));

  p_session = (NET_SECURE_SESSION *)p_sock->SecureSession;

  if (p_session != DEF_NULL) {
    if (p_session->ConnInstance > 0 ) {
      NetSecure_SockCloseNotify(p_sock, p_err);
      if (RTOS_ERR_CODE_GET(*p_err) != RTOS_ERR_NONE) {
        LOG_ERR(("SSL - ", (s)__FUNCTION__, "%: NetSecure_SockCloseNotify() returned: ", (s)RTOS_ERR_STR_GET(RTOS_ERR_CODE_GET(*p_err))));
      }
    }

    if (p_session->DescPtr != DEF_NULL) {
      switch (p_session->Type) {
        case NET_SOCK_SECURE_TYPE_SERVER:
          p_server_desc = (NET_SECURE_SERVER_DESC *)p_session->DescPtr;

          CA_MGMT_freeKeyBlob(&p_server_desc->CertDesc.pKeyBlob);
          CA_MGMT_freeCertificate(&p_server_desc->CertDesc);
          CERT_STORE_releaseStore(&p_server_desc->CertStorePtr);

          Mem_DynPoolBlkFree(&NetSecure_Pools.ServerDescPool, p_server_desc, &err);
          if (RTOS_ERR_CODE_GET(err) != RTOS_ERR_NONE) {
            LOG_ERR(("SSL - ", (s)__FUNCTION__, "%: Mem_DynPoolBlkFree() returned: ", (s)RTOS_ERR_STR_GET(RTOS_ERR_CODE_GET(*p_err))));
          }

          break;

        case NET_SOCK_SECURE_TYPE_CLIENT:
          p_client_desc = (NET_SECURE_CLIENT_DESC *)p_session->DescPtr;

#ifdef __ENABLE_MOCANA_SSL_MUTUAL_AUTH_SUPPORT__

          if (p_client_desc->CertDesc.pKeyBlob != DEF_NULL) {
            CA_MGMT_freeKeyBlob(&p_client_desc->CertDesc.pKeyBlob);
          }

          if (p_client_desc->CertDesc.pCertificate != DEF_NULL) {
            CA_MGMT_freeCertificate(&p_client_desc->CertDesc);
          }

          if (p_client_desc->CertStorePtr != DEF_NULL) {
            CERT_STORE_releaseStore(&p_client_desc->CertStorePtr);
          }
#endif

          Mem_DynPoolBlkFree(&NetSecure_Pools.ClientDescPool, p_client_desc, &err);
          if (RTOS_ERR_CODE_GET(err) != RTOS_ERR_NONE) {
            LOG_ERR(("SSL - ", (s)__FUNCTION__, "%: Mem_DynPoolBlkFree() returned: ", (s)RTOS_ERR_STR_GET(RTOS_ERR_CODE_GET(*p_err))));
          }
          break;

        case NET_SOCK_SECURE_TYPE_NONE:
        default:
          break;
      }
    }

    Mem_DynPoolBlkFree(&NetSecure_Pools.SessionPool, p_sock->SecureSession, &err);
    if (RTOS_ERR_CODE_GET(err) != RTOS_ERR_NONE) {
      LOG_ERR(("SSL - ", (s)__FUNCTION__, "%: Mem_DynPoolBlkFree() returned: ", (s)RTOS_ERR_STR_GET(RTOS_ERR_CODE_GET(*p_err))));
    }
  }

  p_sock->SecureSession = DEF_NULL;

  LOG_VRB(("SSL - ", (s)__FUNCTION__, ": Normal exit"));

  RTOS_ERR_SET(*p_err, RTOS_ERR_NONE);

#else
  RTOS_ERR_SET(*p_err, RTOS_ERR_NOT_AVAIL);
#endif
}

/****************************************************************************************************//**
 *                                       NetSecure_SockCloseNotify()
 *
 * @brief    Transmit the close notify alert to the peer through a SSL session.
 *
 * @param    p_sock  Pointer to a socket.
 *
 * @param    p_err   Pointer to variable that will receive the return error code from this function.
 *
 * @note     (1) If the server decides to close the connection, it SHOULD send a close notify
 *                   alert to the connected peer prior to perform the socket close operations.
 *
 * @note     (2) This function will be called twice during a socket close process but the
 *               close notify alert will only transmitted during the first call.
 *               - (b) The error code that might me returned by 'SSL_shutdown()' is ignored because the
 *                     connection can be closed by the client. In that case, the SSL session will no
 *                     longer be valid and it will be impossible to send the close notify alert through
 *                     that session.
 *******************************************************************************************************/
void NetSecure_SockCloseNotify(NET_SOCK *p_sock,
                               RTOS_ERR *p_err)
{
  sbyte4             status;
  NET_SECURE_SESSION *p_session;

  LOG_VRB(("SSL - ", (s)__FUNCTION__, ": Start"));

  p_session = (NET_SECURE_SESSION *)p_sock->SecureSession;

  if (p_session != DEF_NULL) {
    LOG_DBG(("SSL - ", (s)__FUNCTION__, " :  Close ConnInstance = ", (u)p_session->ConnInstance));
    status = SSL_closeConnection(p_session->ConnInstance);
    if (status != OK) {
      LOG_DBG(("SSL - ", (s)__FUNCTION__, " : SSL_closeConnection() returned: ", (s)(CPU_CHAR *)MERROR_lookUpErrorCode((MSTATUS)status)));
    }

    p_session->ConnInstance = 0u;
  }

  LOG_VRB(("SSL - ", (s)__FUNCTION__, ": Normal exit"));

  RTOS_ERR_SET(*p_err, RTOS_ERR_NONE);
}

/********************************************************************************************************
 *                                       MOCANA CERTIFICATE CALLBACK FUNCTIONS
 *******************************************************************************************************/

/****************************************************************************************************//**
 *                                           NetSecure_MocanaFnctLog()
 *
 * @brief    Mocana log call back function.
 *
 * @param    module      Mocana module.
 *
 * @param    severity    Error severity level.
 *
 * @param    p_msg       Error message to log.
 *******************************************************************************************************/
void NetSecure_MocanaFnctLog(sbyte4 module,
                             sbyte4 severity,
                             sbyte  *p_msg)
{
  PP_UNUSED_PARAM(module);
  PP_UNUSED_PARAM(severity);
  LOG_DBG(("SSL - ", (s)(CPU_CHAR *)p_msg));
}

/****************************************************************************************************//**
 *                                   NetSecure_CertificateStoreVerify()
 *
 * @brief    Verify certificate in store.
 *
 * @param    connectionInstance  SSL connection instance.
 *
 * @param    p_cert              Pointer to the certificate to valid
 *
 * @param    cert_len            Certificate lenght.
 *
 * @param    isSelfSigned        Certificate is self self signed
 *
 * @return   OK, if certificate is trusted.
 *           -1, otherwise.
 *******************************************************************************************************/
static sbyte4 NetSecure_CertificateStoreVerify(sbyte4 conn_instance,
                                               ubyte  *p_cert,
                                               ubyte4 cert_len,
                                               sbyte4 isSelfSigned)
{
#ifdef   NET_SECURE_MODULE_EN
  NET_SOCK               *p_sock = DEF_NULL;
  NET_SECURE_SESSION     *p_session;
  NET_SECURE_CLIENT_DESC *p_client_desc;
  certDistinguishedName  dn;
  CPU_BOOLEAN            trusted;
  CPU_BOOLEAN            result;
  sbyte4                 sock_id;
  sbyte4                 status;

  status = SSL_getSocketId(conn_instance, &sock_id);
  if (status != OK) {
    return (status);
  }

  status = -1;

  p_sock = NetSock_GetObj(sock_id);
  if (p_sock == DEF_NULL) {
    return (status);
  }

  p_session = (NET_SECURE_SESSION *)p_sock->SecureSession;
  if (p_session->Type != NET_SOCK_SECURE_TYPE_CLIENT) {
    return (status);
  }

  if (isSelfSigned == DEF_YES) {
    p_client_desc = (NET_SECURE_CLIENT_DESC *)p_session->DescPtr;
    if (p_client_desc->TrustCallBackFnctPtr != DEF_NULL) {
      status = CA_MGMT_extractCertDistinguishedName(p_cert, cert_len, FALSE, &dn);
      if (status == OK) {
        trusted = p_client_desc->TrustCallBackFnctPtr(&dn, NET_SOCK_SECURE_SELF_SIGNED);
        if (trusted == DEF_YES) {
          status = OK;
        }
      }
    }
  } else {
    result = Mem_Cmp(p_cert, CaCertDesc.pCertificate, cert_len);
    if ((CaCertDesc.certLength == cert_len)
        && (result == DEF_YES)) {
      status = OK;                                              // we trust this cert.
    }
  }

  return (status);

#else
  return (-1);
#endif
}

/****************************************************************************************************//**
 *                                   NetSecure_CertificateStoreLookup()
 *
 * @brief    Find CA certificate in store.
 *
 * @param    conn_instance           SSL connection instance.
 *
 * @param    certDistinguishedName   Received certificate distinguished name.
 *
 * @param    p_return_cert           Pointer to Certificate descriptor
 *
 * @return   OK.
 *******************************************************************************************************/
static sbyte4 NetSecure_CertificateStoreLookup(sbyte4                conn_instance,
                                               certDistinguishedName *p_lookup_cert_dn,
                                               certDescriptor        *p_return_cert)
{
  PP_UNUSED_PARAM(conn_instance);
  PP_UNUSED_PARAM(p_lookup_cert_dn);

  //                                                               For this implementation, we only recognize one ...
  //                                                               ... cert authority.
  p_return_cert->pCertificate = CaCertDesc.pCertificate;
  p_return_cert->certLength = CaCertDesc.certLength;
  p_return_cert->cookie = 0;

  return (OK);
}

/****************************************************************************************************//**
 *                                       NetSecure_CertKeyConvert()
 *
 * @brief    (1) Convert Certificate and Key and allocate memory if needed to store converted certificate and key if needed.
 *               - (a) DER certificate are not converted, the orignal certificate buffer is used.
 *               - (b) PEM certificate are converted to DER certificatr and it is stored in an internal buffer.
 *               - (c) All Keys are converted in Mocana KeyBlog and key is stored in internal buffer.
 *
 * @param    p_cert      Pointer to the certificate
 *
 * @param    cert_size   Certificate length
 *
 * @param    p_key       Pointer to the key
 *
 * @param    key_size    Key length
 *
 * @param    fmt         Certificate and key format
 *
 * @param    p_err       Pointer to variable that will receive the return error code from this function.
 *
 * @return   Certificate descriptor that contain the location of the certificate and the key.
 *******************************************************************************************************/
static certDescriptor NetSecure_CertKeyConvert(const CPU_INT08U             *p_cert,
                                               CPU_SIZE_T                   cert_size,
                                               const CPU_INT08U             *p_key,
                                               CPU_SIZE_T                   key_size,
                                               NET_SOCK_SECURE_CERT_KEY_FMT fmt,
                                               RTOS_ERR                     *p_err)
{
  certDescriptor cert_desc;
  CPU_INT32S     rc;

  RTOS_ERR_SET(*p_err, RTOS_ERR_NONE);

  switch (fmt) {
    case NET_SOCK_SECURE_CERT_KEY_FMT_PEM:
      rc = CA_MGMT_decodeCertificate((ubyte *)p_cert, cert_size, &cert_desc.pCertificate, &cert_desc.certLength);
      if (rc != OK) {
        LOG_ERR(("SSL - ", (s)__FUNCTION__, " : CA_MGMT_decodeCertificate() returned: ", (s)(CPU_CHAR *)MERROR_lookUpErrorCode((MSTATUS)rc)));
        RTOS_ERR_SET(*p_err, RTOS_ERR_FAIL);
        goto exit_fail;
      }

      rc = CA_MGMT_convertKeyPEM((ubyte *)p_key, key_size, &cert_desc.pKeyBlob, &cert_desc.keyBlobLength);
      if (rc != OK) {
        LOG_ERR(("SSL - ", (s)__FUNCTION__, " : CA_MGMT_convertKeyPEM() returned: ", (s)(CPU_CHAR *)MERROR_lookUpErrorCode((MSTATUS)rc)));
        RTOS_ERR_SET(*p_err, RTOS_ERR_FAIL);
        goto exit_fail;
      }
      break;

    case NET_SOCK_SECURE_CERT_KEY_FMT_DER:
      cert_desc.pCertificate = (ubyte *)p_cert;
      cert_desc.certLength = cert_size;
      rc = CA_MGMT_convertKeyDER((ubyte *)p_key, key_size, &cert_desc.pKeyBlob, &cert_desc.keyBlobLength);
      if (rc != OK) {
        LOG_ERR(("SSL - ", (s)__FUNCTION__, " : CA_MGMT_convertKeyDER() returned: ", (s)(CPU_CHAR *)MERROR_lookUpErrorCode((MSTATUS)rc)));
        RTOS_ERR_SET(*p_err, RTOS_ERR_FAIL);
        goto exit_fail;
      }
      break;

#if 0
    case NET_SOCK_SECURE_CERT_KEY_FMT_NATIVE:
      //                                                           We can create a NATIVE format to avoid allocating
      //                                                           memory
      cert_desc.pCertificate = (ubyte *)p_cert;
      cert_desc.certLength = cert_size;
      cert_desc.pKeyBlob = p_key;
      cert_desc.keyBlobLength = key_size;
      break;
#endif

    default:
      RTOS_ERR_SET(*p_err, RTOS_ERR_INVALID_ARG);
      goto exit;
  }

  goto exit;

exit_fail:
  CA_MGMT_freeCertificate(&cert_desc);
  CA_MGMT_freeKeyBlob(&cert_desc.pKeyBlob);

exit:
  return (cert_desc);
}

/********************************************************************************************************
 ********************************************************************************************************
 *                                   DEPENDENCIES & AVAIL CHECK(S) END
 ********************************************************************************************************
 *******************************************************************************************************/

#endif  // RTOS_MODULE_NET_AVAIL && RTOS_MODULE_NET_SSL_TLS_MOCANA_NANOSSL_AVAIL
