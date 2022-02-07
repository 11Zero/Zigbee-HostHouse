/*********************************************************************
注意调整mqtt.h中以下三个值可能是修改,解包的时候使用
#define max_packet_len  300     //封包的最大长度
#define MAX_CMDID_TOPIC_LEN  70   //主题的最大长度
#define MAX_REQ_PAYLOAD_LEN  150   //内容的最大长度
注意需要按实际需要增加MqttKit.h中以下二个枚举数组的定义值，定义数字请参见http://docs.oasis-open.org/mqtt/mqtt/v3.1.1/os/mqtt-v3.1.1-os.html#_Toc398718019 (Table 2.1 - Control packet types)
MqttPacketType
MqttQosLevel
注意本文件中SampleApp_CallBack函数出现2次，猜测为上任作者忘记注释出厂代码，注释掉后无影响

2021年11月7日02点36分 imtht@outlook.com
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

//增加include START
#include "aps_groups.h"
//增加include END

#include "hal_drivers.h"
#include "hal_key.h"
#if defined ( LCD_SUPPORTED )
#include "hal_lcd.h"
#endif
#include "hal_led.h"
#include "hal_uart.h"

#ifdef ZDO_COORDINATOR
//协议文件
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

afAddrType_t SampleApp_P2P_DstAddr;      //点播
//增加变量START
afAddrType_t SampleApp_Periodic_DstAddr;  //广播
aps_Group_t SampleApp_Group;
uint8 DevicesIEEEAddr[4][17]={{"00124B0006F7E953"},{"00124B0006F78A1F"},{"00124B0006F7EAFD"},{"00124B001FBE1D86"}};
uint16 DevicesShortAddr[4]={-1,-1,-1,-1};
int devicesOnline[4]={0,0,0,0};//设备在线状态实时码，跟随终端心跳刷新
int devicesOnlineStatus[4]={0,0,0,0};//设备在线状态码，跟随协调器定时任务刷新
bool flag_COOR_CHECK_or_ENDDEV_HEART_BEAT_EVT=false;//判断SAMPLEAPP_COOR_CHECK_DEV_EVT,SAMPLEAPP_ENDDEV_HEART_BEAT_EVT是否被触发，防止重复触发
uint8 MqttRecMsg[200]={'\0'};//缓存mqtt触发的系统事件消息

uint16 NwkAddr = 0xFFFF;
uint8 MacAddr[8]={0};
uint8 SampleApp_TransID;  // This is the unique message ID (counter)
void SampleApp_getIEEEAddr(uint8* IEEEAddr);//length of IEEEAddr should be Z_EXTADDR_LEN*2
int SampleApp_SendPointToPointMessage(uint16 ReceiveAddr,uint16 DataId,uint8* SendData,uint16 Lens );//ReceiveAddr为接受设备的地址，DataId为数据识别码，4位16进制码，Lens仅写SendData长度即可
void Response2mqtt(uint8* returnMsg);//向服务器发送反馈消息，反馈主题为thing.service.switch:CmdResult中的Result参数
//增加变量END
static uint8 SampleApp_RxBuf[SAMPLE_APP_RX_MAX+1]={0};
static uint16 SampleApp_RxLen=0;

uint8 onenet_login_ok=0;//onenet注册成功 0:未注册，1：注册中　2：注册成功
uint8 end_temp;//终端的温度
uint8 end_hum;//终端的湿度


#ifdef ZDO_COORDINATOR
signed char *g_mqtt_topics_set[5] = {NULL};
u8 topics_buff[60]={0};
u8 topics_post[100]={0}; //发布的主题
u8 mqtt_message[200]={0};//发布的消息
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
  
  MT_UartInit();                  //串口初始化
  MT_UartRegisterTaskID(task_id); //注册串口任务
  afRegister( (endPointDesc_t *)&SampleApp_epDesc );
  RegisterForKeys( task_id );
  //逢蜂鸣器初始化
  
  
#ifdef ZDO_COORDINATOR
  //协调器初始化
  
  
  //默认蜂鸣器不响
  //P0_5=1;
  
  //初始化发布的主题
  //  sprintf(topics_post, "/sys/%s/%s/thing/service/switch:GetSwitchStatus",ProductKey,DeviceName);
  sprintf(topics_post, "/a1sIpEvOQhQ/device/user/transfer");
  //初始化订阅的主题
  sprintf(topics_buff, "/a1sIpEvOQhQ/device/user/receive");
  g_mqtt_topics_set[0]=topics_buff;
#else
  
  P0SEL &= ~0xa0;                 //设置P05和P07为普通IO口
  P0DIR |= 0xa0;                 //P05和P07定义为输出口
#endif
  
  SampleApp_P2P_DstAddr.addrMode = (afAddrMode_t)Addr16Bit; //点播
  SampleApp_P2P_DstAddr.endPoint = SAMPLEAPP_ENDPOINT;
  SampleApp_P2P_DstAddr.addr.shortAddr = 0x0000;            //发给协调器
  
  // Setup for the periodic message's destination address 设置发送数据的方式和目的地址寻址模式
  // Broadcast to everyone 发送模式:广播发送
  SampleApp_Periodic_DstAddr.addrMode = (afAddrMode_t)AddrBroadcast;//广播
  SampleApp_Periodic_DstAddr.endPoint = SAMPLEAPP_ENDPOINT; //指定端点号
  SampleApp_Periodic_DstAddr.addr.shortAddr = 0xFFFF;//指定目的网络地址为广播地址
  
  // By default, all devices start out in Group 1
  SampleApp_Group.ID = 0x0001;//组号
  osal_memcpy( SampleApp_Group.name, "Group 1", 7  );//设定组名
  aps_AddGroup( SAMPLEAPP_ENDPOINT, &SampleApp_Group );//把该组登记添加到APS中
  
#if defined ( LCD_SUPPORTED )
  HalLcdWriteString( "SampleApp", HAL_LCD_LINE_1 ); //如果支持LCD，显示提示信息
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
      //发送温湿度数据到onenet
      //    OneNet_SendData(end_temp, end_hum);
      
      sprintf(mqtt_message,
              "{\"method\":\"thing.service.property.set\",\"id\":\"630262306\",\"params\":{\
                \"code\":2,\
    },\"version\":\"1.0.0\"}"
      );
      //HalLcdWriteString("111", HAL_LCD_LINE_1); //LCD显示
      //发布主题
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
            //这个定时器只是为发送周期信息开启的，设备启动初始化后从这里开始
            //触发第一个周期信息的发送，然后周而复始下去
            if(SampleApp_NwkState == DEV_END_DEVICE)//如果本机作为终端设备接入，尝试向协调器发送本机IEEE,但协调器如何判断接收到的数据是终端的IEEE呢？
            {                                       //考虑数据包前4位设置为识别码，接受方通过识别码判断信息类型
              HalLcdWriteString("get coord", HAL_LCD_LINE_1); //LCD显示
              uint8 IEEEAdr[Z_EXTADDR_LEN*2+1];
              SampleApp_getIEEEAddr(IEEEAdr);
              HalLcdWriteString(IEEEAdr, HAL_LCD_LINE_3); //LCD显示
              SampleApp_SendPointToPointMessage(0x0000,0x1003,IEEEAdr,16);//0x1003代表终端接入时发送的IEEE事件
            } 
          }else if(SampleApp_NwkState == DEV_NWK_ORPHAN)
          {
            HalLcdWriteString("lost coord", HAL_LCD_LINE_1); //LCD显示
          }
          //该处需增加终端离线时触发的事件，尚未查询到如何添加
          osal_start_timerEx( SampleApp_TaskID,
                             SAMPLEAPP_SEND_PERIODIC_MSG_EVT,
                             SAMPLEAPP_SEND_PERIODIC_MSG_TIMEOUT );
          ///////////////////////////////////////////    
#ifdef ZDO_COORDINATOR
          if(!flag_COOR_CHECK_or_ENDDEV_HEART_BEAT_EVT)
          {          
            osal_start_timerEx( SampleApp_TaskID,SAMPLEAPP_COOR_CHECK_DEV_EVT,1000);    //2后检测设备在线状态 
            flag_COOR_CHECK_or_ENDDEV_HEART_BEAT_EVT = true;
          }
#else
          if(!flag_COOR_CHECK_or_ENDDEV_HEART_BEAT_EVT)
          {  
            osal_start_timerEx( SampleApp_TaskID,SAMPLEAPP_ENDDEV_HEART_BEAT_EVT,2000);    //2s后发送一次心跳包到协调器 
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
      
      // Release the memory 事件处理完了，释放消息占用的内存
      osal_msg_deallocate( (uint8 *)MSGpkt );
      // Next - if one is available 指针指向下一个放在缓冲区的待处理的事件，
      //返回while ( MSGpkt )重新处理事件，直到缓冲区没有等待处理事件为止
      MSGpkt = (afIncomingMSGPacket_t *)osal_msg_receive( SampleApp_TaskID );
    }
    
    return ( events ^ SYS_EVENT_MSG );
  }
  
#ifdef ZDO_COORDINATOR
  
  //定时器时间到
  if ( events & SAMPLEAPP_SEND_PERIODIC_MSG_EVT )
  {
    //osal_start_timerEx( SampleApp_TaskID,SAMPLEAPP_COOR_CHECK_DEV_EVT,2000);    //2后检测设备在线状态 
    if(onenet_login_ok==2)
    {
      //如果接入onenet成功，启动心跳和发送数据定旳器
      
      P1_1=0;//点亮D2
      
      
      // 启动心跳定时器，15秒一次
      osal_start_timerEx( SampleApp_TaskID, SAMPLEAPP_ONENET_HEART_BEAT_EVT,15000 );
      
      // 启动发送数据定时器，5秒一次
      //osal_start_timerEx( SampleApp_TaskID, SAMPLEAPP_ONENET_SEND_DATA_EVT,5000 );
    }
    else
    {
      onenet_login_ok=1;//onenet注册成功 0:未注册，1：注册中　2：注册成功
      SampleApp_RxLen=0;
      
      //如果没有接入onenet成功，重新发起接入
      OneNet_DevLink();//接入onenet服务器
      
      //LED2闪，表示正在接入onenet
      HalLedBlink (HAL_LED_2, 5, 50, 500);
      
      // 每5秒尝试接入一次
      osal_start_timerEx( SampleApp_TaskID, SAMPLEAPP_SEND_PERIODIC_MSG_EVT,5000 );
    }
    
    
    // return unprocessed events
    return (events ^ SAMPLEAPP_SEND_PERIODIC_MSG_EVT);
  }
  
  
  //心跳定时器时间到
  if ( events & SAMPLEAPP_ONENET_HEART_BEAT_EVT )
  {
    if(onenet_login_ok==2)
    {
      //发送心跳
      onenet_mqtt_send_heart();
    }
    
    // 启动心跳定时器，15秒一次
    osal_start_timerEx( SampleApp_TaskID, SAMPLEAPP_ONENET_HEART_BEAT_EVT,15000 );
    
    
    // return unprocessed events
    return (events ^ SAMPLEAPP_ONENET_HEART_BEAT_EVT);
  }
  
  
  //发送数据定时器时间到
  if ( events & SAMPLEAPP_ONENET_SEND_DATA_EVT )
  {
    if(onenet_login_ok==2)
    {
      //发送温湿度数据到onenet
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
      );      //该位置数据包应更改为终端数据汇总，汇总后综合发送至mqtt。
      
      //发布主题
      //mqtt_publish_topic(topics_post, mqtt_message);
      
      //  OneNet_publish_topic();
    }
    
    // 启动发送数据定时器，5秒一次发送终端数据汇总
    //osal_start_timerEx( SampleApp_TaskID, SAMPLEAPP_ONENET_SEND_DATA_EVT,5000 );
    
    
    // return unprocessed events
    return (events ^ SAMPLEAPP_ONENET_SEND_DATA_EVT);
  }
  if ( events & SAMPLEAPP_COOR_CHECK_DEV_EVT )//协调器定时检查设备在线状态
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
  
  if ( events & SAMPLEAPP_COOR_CHECK_MQTT_EVT )//协调器执行mqtt处理事件
  {
    uint8 pureRecMsg[50] = {0};
    if(MqttRecMsg[0]=='\0')//假如数据字符为dev1,switch2,1
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
  
  //定时器时间到
  if ( events & SAMPLEAPP_SEND_PERIODIC_MSG_EVT )
  {
    
    // DHT11采集
    //SampleApp_Send_P2P_Message();
    
    // Setup to send message again in normal period (+ a little jitter)
    osal_start_timerEx( SampleApp_TaskID, SAMPLEAPP_SEND_PERIODIC_MSG_EVT,
                       (SAMPLEAPP_SEND_PERIODIC_MSG_TIMEOUT + (osal_rand() & 0x00FF)) );
    
    // return unprocessed events
    return (events ^ SAMPLEAPP_SEND_PERIODIC_MSG_EVT);
  }
  if ( events & SAMPLEAPP_ENDDEV_HEART_BEAT_EVT )//终端向协调器发送心跳包事件
  {
    
    SampleApp_SendPointToPointMessage(0x0000,0x1001,'\0',1);//发送心跳数据包
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
    // 接收终端上传的温度数据
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
      HalLcdWriteString(data, HAL_LCD_LINE_4); //LCD显示
      printf("%s", data);
      //SampleApp_getIEEEAddr(buff);
      //HalLcdWriteString(buff, HAL_LCD_LINE_4); //LCD显示
      switch(dataID_int)
      {
      case 1003://终端向协调器发送IEEE事件，此处自动填写shortAddr映射表DevicesShortAddr，DevicesIEEEAddr
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
      case 1001://心跳包数据事件
        {
          for(int i=0;i<sizeof(devicesOnline)/sizeof(devicesOnline[0]);i++)
          {
            if(pkt->srcAddr.addr.shortAddr == DevicesShortAddr[i])
            {
              devicesOnline[i]=1;//发现心跳终端来信，即设置对应在线状态为在线
              break;
            }
          }
          
        }break;
      case 1002://协调器向终端发送按键消息
        {
          
        }break;
      case 1004://终端收到协调器向终端转发的mqtt消息
        {
          if(NULL!=strstr(data,"switch1,1"))//判断消息设置开关switch1命令为开
          {
            P0_5 = 1;
          }
          else if(NULL!=strstr(data,"switch1,0"))//判断消息设置开关switch1命令为关
          {
            P0_5 = 0;
          }
          else if(NULL!=strstr(data,"switch2,1"))//判断消息设置开关switch2命令为关开
          {
            P0_7 = 1;
          }
          else if(NULL!=strstr(data,"switch2,0"))//判断消息设置开关switch2命令为关
          {
            P0_7 = 0;
          }
        }break;
      }
#ifdef ZDO_COORDINATOR
      if(onenet_login_ok==3)
      {
        //发送温湿度数据到onenet
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

//发送MQTT数据
void ESP8266_SendData(char* buff, int len)
{
  if(len==0) return;
  
  HalUARTWrite(0,buff, len);
}


//接收到下发命令，或者接收到订阅的消息
//topic:收到的主题
//cmd::主题对应的内容
void mqtt_rx(uint8* topic, uint8* cmd)
{
  
  /*
  /a1xaMw4YerO/device/user/receive
  {
  "msg":"balabala",
  "flow":201  //201表示数据流转方向为设备2流向设备1
}
  */
  
  HalLcdWriteString((char*)topic, HAL_LCD_LINE_1); //LCD显示
  HalLcdWriteString((char*)cmd, HAL_LCD_LINE_2); //LCD显示
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
    //          //不知道啥原因，一旦发送消息触发SAMPLEAPP_COOR_CHECK_MQTT_EVT事件，似乎协调器就会死机无法继续发送消息至终端，按键事件也无法发送
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
  
  
  //HalLcdWriteString((char*)topic, HAL_LCD_LINE_1); //LCD显示
  //HalLcdWriteString((char*)msg, HAL_LCD_LINE_2); //LCD显示
  
}


void Response2mqtt(uint8* returnMsg)
{
  sprintf(mqtt_message,"{\"msg\":\"%s\",\"flow\":201}",returnMsg);  
  
  //发布主题
  mqtt_publish_topic(topics_post, mqtt_message);
  
}

bool OneNet_publish_topic(const char *topic, const char *msg)
{ 
  //uint8 buff[100]={0};
  
  //sprintf(buff, "topic:%s,msg:%s", topic, msg);
  
  /*  发布主题为"hello_topic_public"，消息为温度和湿度 */
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
  else if(1==onenet_login_ok)//onenet注册成功 0:未注册，1：注册中　2：注册成功
  {
    //一个一个字节读
    SampleApp_RxLen += HalUARTRead(SAMPLE_APP_PORT, SampleApp_RxBuf+SampleApp_RxLen, 4);
    
    //判断是否是注册成功
    if(SampleApp_RxLen>=4)
    {
      debug("rx len=%d(%x,%x,%x,%x)\r\n", SampleApp_RxLen,SampleApp_RxBuf[0],SampleApp_RxBuf[1],SampleApp_RxBuf[2],SampleApp_RxBuf[3]);
      
      //等待接入响应
      if(MQTT_UnPacketRecv(SampleApp_RxBuf) == MQTT_PKT_CONNACK)
      {
        if(0==MQTT_UnPacketConnectAck(SampleApp_RxBuf))
        {
          onenet_login_ok=2;//注册成功
          SampleApp_RxLen=0;
          
          //请求订阅主题
          mqtt_subscribe_topic(g_mqtt_topics_set, 1);
        }
      }
    }
    
    //数组满，清0
    if(SampleApp_RxLen>=SAMPLE_APP_RX_MAX)
    {
      SampleApp_RxLen=0;
    }
  }
  else if(2==onenet_login_ok)
  {
    //如果是注册成功，读服务器的控制命令
    
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
  
  DHT11();             //获取温湿度
  
  str[0] = wendu;//温度
  str[1] = shidu;//湿度
  len=2;
  
  sprintf(strTemp, "T&H:%d %d", str[0],str[1]);
  HalLcdWriteString(strTemp, HAL_LCD_LINE_3); //LCD显示
  
  HalUARTWrite(0, strTemp, osal_strlen(strTemp));           //串口输出提示信息
  HalUARTWrite(0, "\r\n",2);
  
  //无线发送到协调器
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


//调试信息输出
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
  
  
  HalUARTWrite(0,info, len);//串口0发送
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

int SampleApp_SendPointToPointMessage(uint16 ReceiveAddr,uint16 DataId,uint8* SendData,uint16 Lens )//ReceiveAddr为接受设备的地址，DataId为数据识别码，4位16进制码，Lens仅写SendData长度即可
{
  //uint8 data[10]={'0','1','2','3','4','5','6','7','8','9'};//定义发送内容
  //uint8 data[10]="0123456789";//上述方式二选一 
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
