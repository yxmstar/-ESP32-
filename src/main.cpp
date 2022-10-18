#include<WiFiMulti.h>
#include<Arduino.h>
#include<WebServer.h>
#include<PubsubClient.h>
#include<ArduinoJson.h>

const char* wifiName = "1717";//ESP32连接的WiFi名称
const char* wifiPwd = "wulian1717";//wifi密码
const char* mqttServer = "cdee1c2246.iot-mqtts.cn-north-4.myhuaweicloud.com";//华为云MQTT接入地址
const int   mqtt = 1883;//端口
//下面三个参数为设备接入华为云iot的鉴权参数
const char* clientID = "6346a83e06cae4010b4d1387_esp32_door_0_0_2022101212";
const char* userName = "6346a83e06cae4010b4d1387_esp32_door";
const char* passWord = "6904b5fb8b970b807031e5e645303fe29be2253b5a656ce4a7a0af4ed7fcb517";
const char* topic_report = "$oc/devices/6346a83e06cae4010b4d1387_esp32_door/sys/properties/report";//设备上报
const char* topic_command = "$oc/devices/6346a83e06cae4010b4d1387_esp32_door/sys/commands/#";//设备接收命令
const char* topic_command_response = "$oc/devices/6346a83e06cae4010b4d1387_esp32_door/sys/commands/response/request_id=";//设备发送响应

WiFiMulti wifiMulti;
WiFiClient wifiClient;
PubSubClient client(wifiClient);

int doorState;
int freq = 50;      // 频率(20ms周期)
int channel = 8;    // 通道(高速通道（0 ~ 7）由80MHz时钟驱动，低速通道（8 ~ 15）由 1MHz 时钟驱动。)
int resolution = 8; // 分辨率
const int led = 16;

int calculatePWM(int degree)
{ //0-180度
 //20ms周期，高电平0.5-2.5ms，对应0-180度角度
  const float deadZone = 6.4;//对应0.5ms（0.5ms/(20ms/256）) 舵机转动角度与占空比的关系：(角度/90+0.5)*1023/20
  const float max = 32;//对应2.5ms
  if (degree < 0)
    degree = 0;
  if (degree > 180)
    degree = 180;
  return (int)(((max - deadZone) / 180) * degree + deadZone);
}

void closeDoor()
{
  Serial.println("0\n");
  ledcWrite(channel, calculatePWM(0));
}
void openDoor()
{
    Serial.println("1\n");
  ledcWrite(channel, calculatePWM(180));
}

void MQTT_response(char *topic)
{
String response;

StaticJsonDocument<96> doc;

doc["result_code"] = 0;
doc["response_name"] = "doorControl";
doc["paras"]["doorRes"] = "1";

serializeJson(doc, response);

client.publish(topic,response.c_str());
Serial.println(response);
}

void callback(char *topic,byte *payload,unsigned int length)
{
  //openDoor();
  char *pstr = topic; //指向topic字符串，提取request_id用
 
  /*串口打印出收到的平台消息或者命令*/
  Serial.println();
  Serial.println();
  Serial.print("Message arrived [");
  Serial.print(topic);  //将收到消息的topic展示出来
  Serial.print("] ");
  Serial.println();
 
  payload[length] = '\0'; //在收到的内容后面加上字符串结束符
  char strPayload[255] = {0}; 
  strcpy(strPayload, (const char*)payload);
  Serial.println((char *)payload);  //打印出收到的内容
  Serial.println(strPayload);
 
 
  /*request_id解析部分*///后文有详细解释为什么要提取下发命令的request_id
  char arr[100];  //存放request_id
  int flag = 0;
  char *p = arr;
  while(*pstr)  //以'='为标志，提取出request_id
  {
    if(flag) *p ++ = *pstr;
    if(*pstr == '=') flag = 1;
    pstr++;
  }
  *p = '\0';  
  Serial.println(arr);
 
 
  /*将命令响应topic与resquest_id结合起来*/
  char topicRes[200] = {0};
  strcat(topicRes, topic_command_response);
  strcat(topicRes, arr);
  Serial.println(topicRes);

 // Stream& input;

StaticJsonDocument<192> doc;

DeserializationError error = deserializeJson(doc, payload);

if (error) {
  Serial.print("deserializeJson() failed: ");
  Serial.println(error.c_str());
  return;
}

int paras_doorOpen = doc["paras"]["doorOpen"]; // 1

const char* service_id = doc["service_id"]; // "door"
const char* command_name = doc["command_name"]; // "doorControl"

if(paras_doorOpen == 1)
{
  openDoor();//对应的硬件响应函数
  delay(5000);
ledcWrite(channel, calculatePWM(0));
}else if (paras_doorOpen == 0)
{
  closeDoor();
}

MQTT_response(topicRes);//发送响应参数
}

void MQTT_Report()
{
  String JSONmessageBuffer;//定义字符串接收序列化好的JSON数据
//以下将生成好的JSON格式消息格式化输出到字符数组中，便于下面通过PubSubClient库发送到服务器
StaticJsonDocument<96> doc;

JsonObject services_0 = doc["services"].createNestedObject();
services_0["service_id"] = "door";
services_0["properties"]["doorState"] = doorState;//doorState为全局变量

serializeJson(doc, JSONmessageBuffer);

  Serial.println("Sending message to MQTT topic..");
  Serial.println(JSONmessageBuffer);
  
  if(client.publish(topic_report,JSONmessageBuffer.c_str())==true)//使用c_str函数将string转换为char
  {
    Serial.println("Success sending message");
  }else{
    Serial.println("Error sending message");
  }
  client.loop();//保持硬件活跃度
  Serial.println("---------------");
}

void WifiSetup()
{
  wifiMulti.addAP(wifiName,wifiPwd);//wifi连接
  Serial.print("connecting to:");
  Serial.println(WiFi.SSID());//打印wifi名称
  while(wifiMulti.run() != WL_CONNECTED)
  {
    delay(1000);
    Serial.print(".");
  }
  Serial.println("connection  success!");
  Serial.println("IP address:");
  Serial.println(WiFi.localIP());
}

void MQTT_Init()
{
  client.setServer(mqttServer,mqtt);//设置mqtt服务器参数
  client.setKeepAlive(60);//设置心跳时间
  while(!client.connected())
  {
    Serial.println("Connecting to MQTT...");
    if(client.connect(clientID,userName,passWord))//和华为云iot服务器建立mqtt连接
    {
      Serial.println("connected");
    }else{
      Serial.print("failed with state:");
      Serial.print(client.state());
    }
  }
  client.setCallback(callback);//监听平台下发命令
}

void setup() {
  ledcSetup(channel, freq, resolution); // 设置通道
  ledcAttachPin(led, channel);          // 将通道与对应的引脚连接
  Serial.begin(115200); 
  WifiSetup();
  MQTT_Init();
}

void loop() {
  if (!client.connected()){
    MQTT_Init();
  } 
  else client.loop();
}