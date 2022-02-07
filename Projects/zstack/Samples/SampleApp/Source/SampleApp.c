/*********************************************************************
ע�����mqtt.h����������ֵ�������޸�,�����ʱ��ʹ��
#define max_packet_len  300     //�������󳤶�
#define MAX_CMDID_TOPIC_LEN  70   //�������󳤶�
#define MAX_REQ_PAYLOAD_LEN  150   //���ݵ���󳤶�
ע����Ҫ��ʵ����Ҫ����MqttKit.h�����¶���ö������Ķ���ֵ������������μ�http://docs.oasis-open.org/mqtt/mqtt/v3.1.1/os/mqtt-v3.1.1-os.html#_Toc398718019 (Table 2.1 - Control packet types)
MqttPacketType
MqttQosLevel
ע�Ȿ�ļ���SampleApp_CallBack��������2�Σ��²�Ϊ������������ע�ͳ������룬ע�͵�����Ӱ��

2021��11��7��02��36�� imtht@outlook.com
*/

/*********************************************************************
* INCLUDES
*/

#include <stdio.h>
#include <string.h>
#include "AF.h"
#include "OnBoard.h"
#include "OSAL_Tasks.h"
#include "SampleApp.h"
#include "ZDApp.h"
#include "ZDObject.h"
#include "ZDProfile.h"

//����include START
#include "aps_groups.h"
//����include END

#include "hal_drivers.h"
#include "hal_key.h"
#if defined ( LCD_SUPPORTED )
#include "hal_lcd.h"
#endif
#include "hal_led.h"
#include "hal_uart.h"

#ifdef ZDO_COORDINATOR
//Э���ļ�
#include "mqtt.h"
#include "mqttkit.h"

#else

#include "dht11.h"

#endif


/*********************************************************************
* MACROS
*/

/*********************************************************************
* CONSTANTS
*/

#if !defined( SAMPLE_APP_PORT )
#define SAMPLE_APP_PORT  0
#endif

#if !defined( SAMPLE_APP_RX_MAX )
#define SAMPLE_APP_RX_MAX  300
#endif
#define Z_EXTADDR_LEN   8

// This list should be filled with Application specific Cluster IDs.
const cId_t SampleApp_ClusterList[SAMPLE_MAX_CLUSTERS] =
{
  SAMPLEAPP_P2P_CLUSTERID,
  SAMPLEAPP_PERIODIC_CLUSTERID,
  SERIALAPP_CONNECTREQ_CLUSTER
};

const SimpleDescriptionFormat_t SampleApp_SimpleDesc =
{
  SAMPLEAPP_ENDPOINT,              //  int   Endpoint;
  SAMPLEAPP_PROFID,                //  uint16 AppProfId[2];
  SAMPLEAPP_DEVICEID,              //  uint16 AppDeviceId[2];
  SAMPLEAPP_DEVICE_VERSION,        //  int   AppDevVer:4;
  SAMPLEAPP_FLAGS,                 //  int   AppFlags:4;
  SAMPLE_MAX_CLUSTERS,          //  byte  AppNumInClusters;
  (cId_t *)SampleApp_ClusterList,  //  byte *pAppInClusterList;
  SAMPLE_MAX_CLUSTERS,          //  byte  AppNumOutClusters;
  (cId_t *)SampleApp_ClusterList   //  byte *pAppOutClusterList;
};

endPointDesc_t SampleApp_epDesc =
{
  SAMPLEAPP_ENDPOINT,
  &SampleApp_TaskID,
  (SimpleDescriptionFormat_t *)&SampleApp_SimpleDesc,
  noLatencyReqs
};

/*********************************************************************
* TYPEDEFS
*/

/*********************************************************************
* GLOBAL VARIABLES
*/
devStates_t SampleApp_NwkState;
uint8 SampleApp_TaskID;           // Task ID for internal task/event processing.

/*********************************************************************
* EXTERNAL VARIABLES
*/

/*********************************************************************
* EXTERNAL FUNCTIONS
*/

/*********************************************************************
* LOCAL VARIABLES
*/

static uint8 SampleApp_MsgID;

afAddrType_t SampleApp_P2P_DstAddr;      //�㲥
//���ӱ���START
afAddrType_t SampleApp_Periodic_DstAddr;  //�㲥
aps_Group_t SampleApp_Group;
uint8 DevicesIEEEAddr[4][17]={{"00124B0006F7E953"},{"00124B0006F78A1F"},{"00124B0006F7EAFD"},{"00124B001FBE1D86"}};
uint16 DevicesShortAddr[4]={-1,-1,-1,-1};
int devicesOnline[4]={0,0,0,0};//�豸����״̬ʵʱ�룬�����ն�����ˢ��
int devicesOnlineStatus[4]={0,0,0,0};//�豸����״̬�룬����Э������ʱ����ˢ��
bool flag_COOR_CHECK_or_ENDDEV_HEART_BEAT_EVT=false;//�ж�SAMPLEAPP_COOR_CHECK_DEV_EVT,SAMPLEAPP_ENDDEV_HEART_BEAT_EVT�Ƿ񱻴�������ֹ�ظ�����
uint8 MqttRecMsg[200]={'\0'};//����mqtt������ϵͳ�¼���Ϣ

uint16 NwkAddr = 0xFFFF;
uint8 MacAddr[8]={0};
uint8 SampleApp_TransID;  // This is the unique message ID (counter)
void SampleApp_getIEEEAddr(uint8* IEEEAddr);//length of IEEEAddr should be Z_EXTADDR_LEN*2
int SampleApp_SendPointToPointMessage(uint16 ReceiveAddr,uint16 DataId,uint8* SendData,uint16 Lens );//ReceiveAddrΪ�����豸�ĵ�ַ��DataIdΪ����ʶ���룬4λ16�����룬Lens��дSendData���ȼ���
void Response2mqtt(uint8* returnMsg);//����������ͷ�����Ϣ����������Ϊthing.service.switch:CmdResult�е�Result����
//���ӱ���END
static uint8 SampleApp_RxBuf[SAMPLE_APP_RX_MAX+1]={0};
static uint16 SampleApp_RxLen=0;

uint8 onenet_login_ok=0;//onenetע��ɹ� 0:δע�ᣬ1��ע���С�2��ע��ɹ�
uint8 end_temp;//�ն˵��¶�
uint8 end_hum;//�ն˵�ʪ��


#ifdef ZDO_COORDINATOR
signed char *g_mqtt_topics_set[5] = {NULL};
u8 topics_buff[60]={0};
u8 topics_post[100]={0}; //����������
u8 mqtt_message[200]={0};//��������Ϣ
#endif

/*********************************************************************
* LOCAL FUNCTIONS
*/

static void SampleApp_ProcessMSGCmd( afIncomingMSGPacket_t *pkt );
void SampleApp_CallBack(uint8 port, uint8 event);
static void SampleApp_Send_P2P_Message( void );
bool OneNet_publish_topic(const char *topic, const char *msg);
void debug(uint8 * msg, ...);

/*********************************************************************
* @fn      SampleApp_Init
*
* @brief   This is called during OSAL tasks' initialization.
*
* @param   task_id - the Task ID assigned by OSAL.
*
* @return  none
*/
void SampleApp_Init( uint8 task_id )
{
  halUARTCfg_t uartConfig;
  
  SampleApp_TaskID = task_id;
  SampleApp_NwkState = DEV_INIT;
  
  MT_UartInit();                  //���ڳ�ʼ��
  MT_UartRegisterTaskID(task_id); //ע�ᴮ������
  afRegister( (endPointDesc_t *)&SampleApp_epDesc );
  RegisterForKeys( task_id );
  //���������ʼ��
  
  
#ifdef ZDO_COORDINATOR
  //Э������ʼ��
  
  
  //Ĭ�Ϸ���������
  //P0_5=1;
  
  //��ʼ������������
  //  sprintf(topics_post, "/sys/%s/%s/thing/service/switch:GetSwitchStatus",ProductKey,DeviceName);
  sprintf(topics_post, "/a1sIpEvOQhQ/device/user/transfer");
  //��ʼ�����ĵ�����
  sprintf(topics_buff, "/a1sIpEvOQhQ/device/user/receive");
  g_mqtt_topics_set[0]=topics_buff;
#else
  
  P0SEL &= ~0xa0;                 //����P05��P07Ϊ��ͨIO��
  P0DIR |= 0xa0;                 //P05��P07����Ϊ�����
#endif
  
  SampleApp_P2P_DstAddr.addrMode = (afAddrMode_t)Addr16Bit; //�㲥
  SampleApp_P2P_DstAddr.endPoint = SAMPLEAPP_ENDPOINT;
  SampleApp_P2P_DstAddr.addr.shortAddr = 0x0000;            //����Э����
  
  // Setup for the periodic message's destination address ���÷������ݵķ�ʽ��Ŀ�ĵ�ַѰַģʽ
  // Broadcast to everyone ����ģʽ:�㲥����
  SampleApp_Periodic_DstAddr.addrMode = (afAddrMode_t)AddrBroadcast;//�㲥
  SampleApp_Periodic_DstAddr.endPoint = SAMPLEAPP_ENDPOINT; //ָ���˵��
  SampleApp_Periodic_DstAddr.addr.shortAddr = 0xFFFF;//ָ��Ŀ�������ַΪ�㲥��ַ
  
  // By default, all devices start out in Group 1
  SampleApp_Group.ID = 0x0001;//���
  osal_memcpy( SampleApp_Group.name, "Group 1", 7  );//�趨����
  aps_AddGroup( SAMPLEAPP_ENDPOINT, &SampleApp_Group );//�Ѹ���Ǽ���ӵ�APS��
  
#if defined ( LCD_SUPPORTED )
  HalLcdWriteString( "SampleApp", HAL_LCD_LINE_1 ); //���֧��LCD����ʾ��ʾ��Ϣ
#endif
  
  NwkAddr = NLME_GetShortAddr();//maybe unused
  memcpy(MacAddr,NLME_GetExtAddr(),8);//maybe unused
  
}

void SampleApp_HandleKeys( uint8 shift, uint8 keys )
{
  (void)shift;  // Intentionally unreferenced parameter
  
#if defined(ZDO_COORDINATOR)
  
  if ( keys & HAL_KEY_SW_6 )//key1
  {
    SampleApp_SendPointToPointMessage(DevicesShortAddr[2],0x1002,"1002 msg",16);
    //    const signed char *g_mqtt_topics[] = {"mqtt_topic_test1"};
    //    if(0 == mqtt_subscribe_topic(g_mqtt_topics, 1))
    //    {
    //      
    //    }
  }
  
  if ( keys & HAL_KEY_SW_1 )//key2
  {
    SampleApp_SendPointToPointMessage(DevicesShortAddr[2],0x1004,"1004 msg",70);
    //    if(0==SampleApp_SendPointToPointMessage(DevicesShortAddr[2],0x1004,MqttRecMsg,strlen(MqttRecMsg)))
    //    {
    //      memset(MqttRecMsg,0,sizeof(MqttRecMsg));
    //    }
    if(onenet_login_ok==2)
    {
      //������ʪ�����ݵ�onenet
      //    OneNet_SendData(end_temp, end_hum);
      
      sprintf(mqtt_message,
              "{\"method\":\"thing.service.property.set\",\"id\":\"630262306\",\"params\":{\
                \"code\":2,\
    },\"version\":\"1.0.0\"}"
      );
      //HalLcdWriteString("111", HAL_LCD_LINE_1); //LCD��ʾ
      //��������
      mqtt_publish_topic(topics_post, mqtt_message);
      
      //  OneNet_publish_topic();
    }
  }
  
#else
  if ( keys & HAL_KEY_SW_6 )//key1
  {
    
    
    SampleApp_SendPointToPointMessage(0x0000,0x1001,"1001 msg",16);
    //P0_5 = 1;
    //HalLcdWriteString("P0_5 = 1", HAL_LCD_LINE_3);
  }
  
  if ( keys & HAL_KEY_SW_1 )//key2
  {
    
    SampleApp_SendPointToPointMessage(0x0000,0x1002,"1002 msg",16);
    //P0_5 = 0;
    //HalLcdWriteString("P0_5 = 0", HAL_LCD_LINE_3);
  }
  
#endif
}


/*********************************************************************
* @fn      SampleApp_ProcessEvent
*
* @brief   Generic Application Task event processor.
*
* @param   task_id  - The OSAL assigned task ID.
* @param   events   - Bit map of events to process.
*
* @return  Event flags of all unprocessed events.
*/
UINT16 SampleApp_ProcessEvent( uint8 task_id, UINT16 events )
{
  (void)task_id;  // Intentionally unreferenced parameter
  
  if ( events & SYS_EVENT_MSG )
  {
    afIncomingMSGPacket_t *MSGpkt;
    
    while ( (MSGpkt = (afIncomingMSGPacket_t *)osal_msg_receive( SampleApp_TaskID )) )
    {
      switch ( MSGpkt->hdr.event )
      {
      case KEY_CHANGE:
        SampleApp_HandleKeys( ((keyChange_t *)MSGpkt)->state, ((keyChange_t *)MSGpkt)->keys );
        break;
        
      case AF_INCOMING_MSG_CMD:
        SampleApp_ProcessMSGCmd( MSGpkt );
        break;
        
      case ZDO_STATE_CHANGE:
        {
          
          /////////////////////////////////////
#ifdef ZDO_COORDINATOR
          SampleApp_NwkState = (zdoStatus_t)(MSGpkt->hdr.status);
#else
          SampleApp_NwkState = (devStates_t)(MSGpkt->hdr.status);
#endif
          ////////////////////////////////////////// 
          if ( (SampleApp_NwkState == DEV_ROUTER) || (SampleApp_NwkState == DEV_END_DEVICE) )
          {
            // Start sending the periodic message in a regular interval.
            //�����ʱ��ֻ��Ϊ����������Ϣ�����ģ��豸������ʼ��������￪ʼ
            //������һ��������Ϣ�ķ��ͣ�Ȼ���ܶ���ʼ��ȥ
            if(SampleApp_NwkState == DEV_END_DEVICE)//���������Ϊ�ն��豸���룬������Э�������ͱ���IEEE,��Э��������жϽ��յ����������ն˵�IEEE�أ�
            {                                       //�������ݰ�ǰ4λ����Ϊʶ���룬���ܷ�ͨ��ʶ�����ж���Ϣ����
              HalLcdWriteString("get coord", HAL_LCD_LINE_1); //LCD��ʾ
              uint8 IEEEAdr[Z_EXTADDR_LEN*2+1];
              SampleApp_getIEEEAddr(IEEEAdr);
              HalLcdWriteString(IEEEAdr, HAL_LCD_LINE_3); //LCD��ʾ
              SampleApp_SendPointToPointMessage(0x0000,0x1003,IEEEAdr,16);//0x1003�����ն˽���ʱ���͵�IEEE�¼�
            } 
          }else if(SampleApp_NwkState == DEV_NWK_ORPHAN)
          {
            HalLcdWriteString("lost coord", HAL_LCD_LINE_1); //LCD��ʾ
          }
          //�ô��������ն�����ʱ�������¼�����δ��ѯ��������
          osal_start_timerEx( SampleApp_TaskID,
                             SAMPLEAPP_SEND_PERIODIC_MSG_EVT,
                             SAMPLEAPP_SEND_PERIODIC_MSG_TIMEOUT );
          ///////////////////////////////////////////    
#ifdef ZDO_COORDINATOR
          if(!flag_COOR_CHECK_or_ENDDEV_HEART_BEAT_EVT)
          {          
            osal_start_timerEx( SampleApp_TaskID,SAMPLEAPP_COOR_CHECK_DEV_EVT,1000);    //2�����豸����״̬ 
            flag_COOR_CHECK_or_ENDDEV_HEART_BEAT_EVT = true;
          }
#else
          if(!flag_COOR_CHECK_or_ENDDEV_HEART_BEAT_EVT)
          {  
            osal_start_timerEx( SampleApp_TaskID,SAMPLEAPP_ENDDEV_HEART_BEAT_EVT,2000);    //2s����һ����������Э���� 
            flag_COOR_CHECK_or_ENDDEV_HEART_BEAT_EVT = true;
          }
#endif
          //////////////////////////////////////   
          //        }
          //        else if(SampleApp_NwkState == DEV_ZB_COORD)
          //        {
          //          osal_start_timerEx( SampleApp_TaskID,
          //                             SAMPLEAPP_SEND_PERIODIC_MSG_EVT,
          //                             SAMPLEAPP_SEND_PERIODIC_MSG_TIMEOUT );
          //        }
        }break;
        
      default:
        break;
      }
      
      // Release the memory �¼��������ˣ��ͷ���Ϣռ�õ��ڴ�
      osal_msg_deallocate( (uint8 *)MSGpkt );
      // Next - if one is available ָ��ָ����һ�����ڻ������Ĵ�������¼���
      //����while ( MSGpkt )���´����¼���ֱ��������û�еȴ������¼�Ϊֹ
      MSGpkt = (afIncomingMSGPacket_t *)osal_msg_receive( SampleApp_TaskID );
    }
    
    return ( events ^ SYS_EVENT_MSG );
  }
  
#ifdef ZDO_COORDINATOR
  
  //��ʱ��ʱ�䵽
  if ( events & SAMPLEAPP_SEND_PERIODIC_MSG_EVT )
  {
    //osal_start_timerEx( SampleApp_TaskID,SAMPLEAPP_COOR_CHECK_DEV_EVT,2000);    //2�����豸����״̬ 
    if(onenet_login_ok==2)
    {
      //�������onenet�ɹ������������ͷ������ݶ��A��
      
      P1_1=0;//����D2
      
      
      // ����������ʱ����15��һ��
      osal_start_timerEx( SampleApp_TaskID, SAMPLEAPP_ONENET_HEART_BEAT_EVT,15000 );
      
      // �����������ݶ�ʱ����5��һ��
      //osal_start_timerEx( SampleApp_TaskID, SAMPLEAPP_ONENET_SEND_DATA_EVT,5000 );
    }
    else
    {
      onenet_login_ok=1;//onenetע��ɹ� 0:δע�ᣬ1��ע���С�2��ע��ɹ�
      SampleApp_RxLen=0;
      
      //���û�н���onenet�ɹ������·������
      OneNet_DevLink();//����onenet������
      
      //LED2������ʾ���ڽ���onenet
      HalLedBlink (HAL_LED_2, 5, 50, 500);
      
      // ÿ5�볢�Խ���һ��
      osal_start_timerEx( SampleApp_TaskID, SAMPLEAPP_SEND_PERIODIC_MSG_EVT,5000 );
    }
    
    
    // return unprocessed events
    return (events ^ SAMPLEAPP_SEND_PERIODIC_MSG_EVT);
  }
  
  
  //������ʱ��ʱ�䵽
  if ( events & SAMPLEAPP_ONENET_HEART_BEAT_EVT )
  {
    if(onenet_login_ok==2)
    {
      //��������
      onenet_mqtt_send_heart();
    }
    
    // ����������ʱ����15��һ��
    osal_start_timerEx( SampleApp_TaskID, SAMPLEAPP_ONENET_HEART_BEAT_EVT,15000 );
    
    
    // return unprocessed events
    return (events ^ SAMPLEAPP_ONENET_HEART_BEAT_EVT);
  }
  
  
  //�������ݶ�ʱ��ʱ�䵽
  if ( events & SAMPLEAPP_ONENET_SEND_DATA_EVT )
  {
    if(onenet_login_ok==2)
    {
      //������ʪ�����ݵ�onenet
      //    OneNet_SendData(end_temp, end_hum);
      
      sprintf(mqtt_message,"{\"flow\":201,\
        \"msg\":{\
          \"dev1\":%d,\
            \"dev2\":%d,\
              \"dev3\":%d,\
    }}",
    devicesOnlineStatus[0],
    devicesOnlineStatus[1],
    devicesOnlineStatus[2]
      );      //��λ�����ݰ�Ӧ����Ϊ�ն����ݻ��ܣ����ܺ��ۺϷ�����mqtt��
      
      //��������
      //mqtt_publish_topic(topics_post, mqtt_message);
      
      //  OneNet_publish_topic();
    }
    
    // �����������ݶ�ʱ����5��һ�η����ն����ݻ���
    //osal_start_timerEx( SampleApp_TaskID, SAMPLEAPP_ONENET_SEND_DATA_EVT,5000 );
    
    
    // return unprocessed events
    return (events ^ SAMPLEAPP_ONENET_SEND_DATA_EVT);
  }
  if ( events & SAMPLEAPP_COOR_CHECK_DEV_EVT )//Э������ʱ����豸����״̬
  {
    uint8 buff[40]={0};
    
    for(int i=0;i<sizeof(devicesOnline)/sizeof(devicesOnline[0]);i++)
    {
      devicesOnlineStatus[i] = devicesOnline[i];
      devicesOnline[i]=0;
      sprintf(buff,"device%d %d",i,devicesOnlineStatus[i]);
      //HalLcdWriteString(buff, i+1);
    }
    
    osal_start_timerEx( SampleApp_TaskID, SAMPLEAPP_COOR_CHECK_DEV_EVT,5000 );
    return (events ^ SAMPLEAPP_COOR_CHECK_DEV_EVT);
  }
  
  if ( events & SAMPLEAPP_COOR_CHECK_MQTT_EVT )//Э����ִ��mqtt�����¼�
  {
    uint8 pureRecMsg[50] = {0};
    if(MqttRecMsg[0]=='\0')//���������ַ�Ϊdev1,switch2,1
    {}
    //else if(strstr(MqttRecMsg,"dev2,")!=NULL)
    else{
      printf("MqttRecMsg:%s\n",MqttRecMsg);
      if(strstr(MqttRecMsg,"\"}")!=NULL)
      {
        memcpy(pureRecMsg,strstr(MqttRecMsg,"\"msg\":\"")+7,strstr(MqttRecMsg,"\"}")-(strstr(MqttRecMsg,"\"msg\":\"")+7));
      }else if(strstr(MqttRecMsg,"\",\"flow\"")!=NULL)
      {
        memcpy(pureRecMsg,strstr(MqttRecMsg,"\"msg\":\"")+7,strstr(MqttRecMsg,"\",\"flow\"")-(strstr(MqttRecMsg,"\"msg\":\"")+7));
      }
    }
    if(DevicesShortAddr[0]!=-1 && strstr(pureRecMsg,"dev0,")!=NULL)
    {
      SampleApp_SendPointToPointMessage(DevicesShortAddr[0],0x1004,pureRecMsg,strlen(pureRecMsg)+1);
      printf("dev0,");
    }else if(DevicesShortAddr[1]!=-1 && strstr(pureRecMsg,"dev1,")!=NULL)
    {
      SampleApp_SendPointToPointMessage(DevicesShortAddr[1],0x1004,pureRecMsg,strlen(pureRecMsg)+1);
      printf("dev1,");
    }else if(DevicesShortAddr[2]!=-1 && strstr(pureRecMsg,"dev2,")!=NULL)
    {
      SampleApp_SendPointToPointMessage(DevicesShortAddr[2],0x1004,pureRecMsg,strlen(pureRecMsg)+1);
      printf("dev2,");
    }else if(DevicesShortAddr[3]!=-1 && strstr(pureRecMsg,"dev3,")!=NULL)
    {
      SampleApp_SendPointToPointMessage(DevicesShortAddr[3],0x1004,pureRecMsg,strlen(pureRecMsg)+1);
      printf("dev3,");
    }
    osal_memset(MqttRecMsg, 0, 200);
    
    return (events ^ SAMPLEAPP_COOR_CHECK_MQTT_EVT);
  }  
#else
  
  //��ʱ��ʱ�䵽
  if ( events & SAMPLEAPP_SEND_PERIODIC_MSG_EVT )
  {
    
    // DHT11�ɼ�
    //SampleApp_Send_P2P_Message();
    
    // Setup to send message again in normal period (+ a little jitter)
    osal_start_timerEx( SampleApp_TaskID, SAMPLEAPP_SEND_PERIODIC_MSG_EVT,
                       (SAMPLEAPP_SEND_PERIODIC_MSG_TIMEOUT + (osal_rand() & 0x00FF)) );
    
    // return unprocessed events
    return (events ^ SAMPLEAPP_SEND_PERIODIC_MSG_EVT);
  }
  if ( events & SAMPLEAPP_ENDDEV_HEART_BEAT_EVT )//�ն���Э���������������¼�
  {
    
    SampleApp_SendPointToPointMessage(0x0000,0x1001,'\0',1);//�����������ݰ�
    osal_start_timerEx( SampleApp_TaskID, SAMPLEAPP_ENDDEV_HEART_BEAT_EVT,2000 );
    
    // return unprocessed events
    return (events ^ SAMPLEAPP_ENDDEV_HEART_BEAT_EVT);
  }
  
#endif
  
  return ( 0 );  // Discard unknown events.
}

/*********************************************************************
* @fn      SerialApp_ProcessMSGCmd
*
* @brief   Data message processor callback. This function processes
*          any incoming data - probably from other devices. Based
*          on the cluster ID, perform the intended action.
*
* @param   pkt - pointer to the incoming message packet
*
* @return  TRUE if the 'pkt' parameter is being used and will be freed later,
*          FALSE otherwise.
*/
void SampleApp_ProcessMSGCmd( afIncomingMSGPacket_t *pkt )
{
  //uint8 buff[200]={0};
  
  //uint8 dataID[5]={0};
  int dataID_int = 0;
  int dataLens = 0;
  uint8 data[200]={0};
  //uint8 tmpData[200]={0};
  switch ( pkt->clusterId )
  {
    // �����ն��ϴ����¶�����
  case SAMPLEAPP_P2P_CLUSTERID:
    
    {
      
      memcpy(data,pkt->cmd.Data,4);
      data[4]='\0';
      sscanf((char*)data, "%4d", &dataID_int ); 
      memcpy(data,(pkt->cmd.Data)+4,4);
      data[4]='\0';
      sscanf((char*)data, "%4d", &dataLens ); 
      memcpy(data,pkt->cmd.Data+8,dataLens-8);
      data[dataLens-8]='\0';
      
      
      //sprintf(buff, "%s", pkt->cmd.Data);
      HalLcdWriteString(data, HAL_LCD_LINE_4); //LCD��ʾ
      printf("%s", data);
      //SampleApp_getIEEEAddr(buff);
      //HalLcdWriteString(buff, HAL_LCD_LINE_4); //LCD��ʾ
      switch(dataID_int)
      {
      case 1003://�ն���Э��������IEEE�¼����˴��Զ���дshortAddrӳ���DevicesShortAddr��DevicesIEEEAddr
        {
          for(int i=0;i<sizeof(DevicesIEEEAddr)/sizeof(DevicesIEEEAddr[0]);i++)
          {
            if(strncmp((char*)data,(char*)DevicesIEEEAddr[i],16)==0)
            {
              DevicesShortAddr[i]=pkt->srcAddr.addr.shortAddr;
              sprintf((char*)data,"device %d linked",i);
              //HalLcdWriteString((char*)tmpData,i+1);
              //SampleApp_getIEEEAddr(data);
              //              SampleApp_SendPointToPointMessage(DevicesShortAddr[i],0x1004,tmpData,16);
#ifdef ZDO_COORDINATOR
              if(onenet_login_ok==2)
              {
                sprintf(mqtt_message, "{\"flow\":201,\"msg\":\"%s Linked\"}",DevicesIEEEAddr[i]);
                OneNet_publish_topic(topics_post, mqtt_message);
              }
#endif
              break;
            }
          }
        }break;
      case 1001://�����������¼�
        {
          for(int i=0;i<sizeof(devicesOnline)/sizeof(devicesOnline[0]);i++)
          {
            if(pkt->srcAddr.addr.shortAddr == DevicesShortAddr[i])
            {
              devicesOnline[i]=1;//���������ն����ţ������ö�Ӧ����״̬Ϊ����
              break;
            }
          }
          
        }break;
      case 1002://Э�������ն˷��Ͱ�����Ϣ
        {
          
        }break;
      case 1004://�ն��յ�Э�������ն�ת����mqtt��Ϣ
        {
          if(NULL!=strstr(data,"switch1,1"))//�ж���Ϣ���ÿ���switch1����Ϊ��
          {
            P0_5 = 1;
          }
          else if(NULL!=strstr(data,"switch1,0"))//�ж���Ϣ���ÿ���switch1����Ϊ��
          {
            P0_5 = 0;
          }
          else if(NULL!=strstr(data,"switch2,1"))//�ж���Ϣ���ÿ���switch2����Ϊ�ؿ�
          {
            P0_7 = 1;
          }
          else if(NULL!=strstr(data,"switch2,0"))//�ж���Ϣ���ÿ���switch2����Ϊ��
          {
            P0_7 = 0;
          }
        }break;
      }
#ifdef ZDO_COORDINATOR
      if(onenet_login_ok==3)
      {
        //������ʪ�����ݵ�onenet
        //    OneNet_SendData(end_temp, end_hum);
        
        sprintf(mqtt_message, 
                "{\"method\":\"thing.service.property.set\",\"id\":\"630262306\",\"params\":{\
                  \"code\":\"%s\"\
      },\"version\":\"1.0.0\"}",
      data);
      //mqtt_publish_topic(topics_post, mqtt_message);
      
      //  OneNet_publish_topic();
      }
#endif
    }break;
    
  case SAMPLEAPP_PERIODIC_CLUSTERID:
    
    break;
    
  default:
    break;
  }
}

#ifdef ZDO_COORDINATOR

//����MQTT����
void ESP8266_SendData(char* buff, int len)
{
  if(len==0) return;
  
  HalUARTWrite(0,buff, len);
}


//���յ��·�������߽��յ����ĵ���Ϣ
//topic:�յ�������
//cmd::�����Ӧ������
void mqtt_rx(uint8* topic, uint8* cmd)
{
  
  /*
  /a1xaMw4YerO/device/user/receive
  {
  "msg":"balabala",
  "flow":201  //201��ʾ������ת����Ϊ�豸2�����豸1
}
  */
  
  HalLcdWriteString((char*)topic, HAL_LCD_LINE_1); //LCD��ʾ
  HalLcdWriteString((char*)cmd, HAL_LCD_LINE_2); //LCD��ʾ
  if(topic == NULL || cmd==NULL) return;
  uint8 buff[100]={0};
  uint8 method[100]={0};
  uint8 msg[100]={0};
  uint8* p_buff = NULL;
  memcpy(buff,topic,strlen(topic)+1);
  p_buff = strrchr(buff,'/')+1;
  memcpy(method,p_buff,strlen(p_buff)+1);
  
  
  if(0==strcmp(method,"receive"))
  {
    //          memcpy(MqttRecMsg,cmd,strlen(cmd));  
    //          osal_start_timerEx( SampleApp_TaskID, SAMPLEAPP_COOR_CHECK_MQTT_EVT,1000);
    //          //��֪��ɶԭ��һ��������Ϣ����SAMPLEAPP_COOR_CHECK_MQTT_EVT�¼����ƺ�Э�����ͻ������޷�����������Ϣ���նˣ������¼�Ҳ�޷�����
    //          HalLcdWriteString((char*)MqttRecMsg, HAL_LCD_LINE_3);
    if(MqttRecMsg[0]=='\0')
    {
      memcpy(MqttRecMsg,cmd,strlen(cmd));  
      osal_start_timerEx( SampleApp_TaskID, SAMPLEAPP_COOR_CHECK_MQTT_EVT,10 );
      HalLcdWriteString((char*)MqttRecMsg, HAL_LCD_LINE_3);
    }
    else{
      //osal_start_timerEx( SampleApp_TaskID, SAMPLEAPP_COOR_CHECK_MQTT_EVT,5 );
      Response2mqtt("too many msg, hold on");
      HalLcdWriteString("too many msg", HAL_LCD_LINE_4);
    }
  }
  
  
  //HalLcdWriteString((char*)topic, HAL_LCD_LINE_1); //LCD��ʾ
  //HalLcdWriteString((char*)msg, HAL_LCD_LINE_2); //LCD��ʾ
  
}


void Response2mqtt(uint8* returnMsg)
{
  sprintf(mqtt_message,"{\"msg\":\"%s\",\"flow\":201}",returnMsg);  
  
  //��������
  mqtt_publish_topic(topics_post, mqtt_message);
  
}

bool OneNet_publish_topic(const char *topic, const char *msg)
{ 
  //uint8 buff[100]={0};
  
  //sprintf(buff, "topic:%s,msg:%s", topic, msg);
  
  /*  ��������Ϊ"hello_topic_public"����ϢΪ�¶Ⱥ�ʪ�� */
  return mqtt_publish_topic(topic, msg);
}



/*********************************************************************
* @fn      SampleApp_CallBack
*
* @brief   Send data OTA.
*
* @param   port - UART port.
* @param   event - the UART port event flag.
*
* @return  none
*/
uint32 lastReadMs=0;
void SampleApp_CallBack(uint8 port, uint8 event)
{
  (void)port;
  
  if(0==onenet_login_ok)
  {
    HalUARTRead(SAMPLE_APP_PORT, SampleApp_RxBuf, SAMPLE_APP_RX_MAX);
    SampleApp_RxLen=0;
    osal_memset(SampleApp_RxBuf, 0, SAMPLE_APP_RX_MAX);
  }
  else if(1==onenet_login_ok)//onenetע��ɹ� 0:δע�ᣬ1��ע���С�2��ע��ɹ�
  {
    //һ��һ���ֽڶ�
    SampleApp_RxLen += HalUARTRead(SAMPLE_APP_PORT, SampleApp_RxBuf+SampleApp_RxLen, 4);
    
    //�ж��Ƿ���ע��ɹ�
    if(SampleApp_RxLen>=4)
    {
      debug("rx len=%d(%x,%x,%x,%x)\r\n", SampleApp_RxLen,SampleApp_RxBuf[0],SampleApp_RxBuf[1],SampleApp_RxBuf[2],SampleApp_RxBuf[3]);
      
      //�ȴ�������Ӧ
      if(MQTT_UnPacketRecv(SampleApp_RxBuf) == MQTT_PKT_CONNACK)
      {
        if(0==MQTT_UnPacketConnectAck(SampleApp_RxBuf))
        {
          onenet_login_ok=2;//ע��ɹ�
          SampleApp_RxLen=0;
          
          //����������
          mqtt_subscribe_topic(g_mqtt_topics_set, 1);
        }
      }
    }
    
    //����������0
    if(SampleApp_RxLen>=SAMPLE_APP_RX_MAX)
    {
      SampleApp_RxLen=0;
    }
  }
  else if(2==onenet_login_ok)
  {
    //�����ע��ɹ������������Ŀ�������
    
    uint32 curMs=osal_GetSystemClock();
    
    if((curMs-lastReadMs)<1000) return;
    
    lastReadMs=curMs;
    SampleApp_RxLen=0;
    osal_memset(SampleApp_RxBuf, 0, SAMPLE_APP_RX_MAX+1);
    SampleApp_RxLen = HalUARTRead(SAMPLE_APP_PORT, SampleApp_RxBuf, SAMPLE_APP_RX_MAX);
    
    
    
    if(SampleApp_RxLen>0)
    {
      debug("rx len=%d.\r\n", SampleApp_RxLen);
      
      OneNet_RevPro(SampleApp_RxBuf);// /sys/a1VZAPhhRWF/zigbee-coor/thing/service/property/set {"method":...}
      SampleApp_RxLen=0;
      osal_memset(SampleApp_RxBuf, 0, SAMPLE_APP_RX_MAX+1);
    }
  }
  
}

#else

/*********************************************************************
* @fn      SampleApp_Send_P2P_Message
*
* @brief   point to point.
*
* @param   none
*
* @return  none
*/
void SampleApp_Send_P2P_Message( void )
{
  uint8 str[5]={0};
  uint8 strTemp[20]={0};
  int len=0;
  
  DHT11();             //��ȡ��ʪ��
  
  str[0] = wendu;//�¶�
  str[1] = shidu;//ʪ��
  len=2;
  
  sprintf(strTemp, "T&H:%d %d", str[0],str[1]);
  HalLcdWriteString(strTemp, HAL_LCD_LINE_3); //LCD��ʾ
  
  HalUARTWrite(0, strTemp, osal_strlen(strTemp));           //���������ʾ��Ϣ
  HalUARTWrite(0, "\r\n",2);
  
  //���߷��͵�Э����
  if ( AF_DataRequest( &SampleApp_P2P_DstAddr, &SampleApp_epDesc,
                      SAMPLEAPP_P2P_CLUSTERID,
                      len,
                      str,
                      &SampleApp_MsgID,
                      AF_DISCV_ROUTE,
                      AF_DEFAULT_RADIUS ) == afStatus_SUCCESS )
  {
  }
  else
  {
    // Error occurred in request to send.
  }
}

void SampleApp_CallBack(uint8 port, uint8 event)
{
  (void)port;
  
  HalUARTRead(SAMPLE_APP_PORT, SampleApp_RxBuf, 1);
  
}


#endif


//������Ϣ���
void debug(uint8 * msg, ...)
{
#if 0
  uint8 len=0;
  char info[100] = {0};
  va_list args;
  
  if(msg==NULL) return;
  
  va_start(args, msg);
  vsprintf(info, (const char*)msg, args);
  va_end(args);
  
  len=osal_strlen((char *)info);
  if(len==0) return;
  
  
  HalUARTWrite(0,info, len);//����0����
#endif
}

void SampleApp_getIEEEAddr(uint8* IEEEAddr)//length of IEEEAddr should be Z_EXTADDR_LEN*2
{
  
  uint8 i;
  uint8 *xad;
  //uint8 lcd_buf[Z_EXTADDR_LEN*2+1];
  
  // Display the extended address.
  xad = aExtendedAddress + Z_EXTADDR_LEN - 1;
  
  for (i = 0; i < Z_EXTADDR_LEN*2; xad--)
  {
    uint8 ch;
    ch = (*xad >> 4) & 0x0F;
    IEEEAddr[i++] = ch + (( ch < 10 ) ? '0' : '7');
    ch = *xad & 0x0F;
    IEEEAddr[i++] = ch + (( ch < 10 ) ? '0' : '7');
  }
  IEEEAddr[Z_EXTADDR_LEN*2] = '\0';
}

int SampleApp_SendPointToPointMessage(uint16 ReceiveAddr,uint16 DataId,uint8* SendData,uint16 Lens )//ReceiveAddrΪ�����豸�ĵ�ַ��DataIdΪ����ʶ���룬4λ16�����룬Lens��дSendData���ȼ���
{
  //uint8 data[10]={'0','1','2','3','4','5','6','7','8','9'};//���巢������
  //uint8 data[10]="0123456789";//������ʽ��ѡһ 
  SampleApp_P2P_DstAddr.addr.shortAddr = ReceiveAddr;
  uint8 FinalSendData[200]={0};
  sprintf((char*)FinalSendData,"%4x%4d%s",DataId,Lens+8,(char*)SendData);
  printf("FinalSendData:%s\n",FinalSendData);
  int result = AF_DataRequest(&SampleApp_P2P_DstAddr,&SampleApp_epDesc,
                              SAMPLEAPP_P2P_CLUSTERID,
                              Lens+8,
                              FinalSendData,
                              &SampleApp_TransID,
                              AF_DISCV_ROUTE,
                              AF_DEFAULT_RADIUS);
  if(result == afStatus_SUCCESS)
  {
    return 0;
  }
  else
  {
    printf("%d",result);
    return result;
    // Error occurred in request to send.
  }
}
