//
// fsm.c
// FSM sample code
//
// Created by Minsuk Lee, 2014.11.1.
// Copyright (c) 2014. Minsuk Lee All rights reserved.
// see LICENSE

#include "util.h"

#define MAX_DATA_SIZE   (500)

#define CONNECT_TIMEOUT 2

#define NUM_STATE   3
#define NUM_EVENT   8

enum pakcet_type { F_CON = 0, F_FIN = 1, F_ACK = 2, F_DATA = 3 };   // Packet Type
enum proto_state { wait_CON = 0, CON_sent = 1, CONNECTED = 2 };     // States

// Events
enum proto_event { RCV_CON = 0, RCV_FIN = 1, RCV_ACK = 2, RCV_DATA = 3,
                   CONNECT = 4, CLOSE = 5,   SEND = 6,    TIMEOUT = 7 };

char *pkt_name[] = { "F_CON", "F_FIN", "F_ACK", "F_DATA" };
char *st_name[] =  { "wait_CON", "CON_sent", "CONNECTED" };
char *ev_name[] =  { "RCV_CON", "RCV_FIN", "RCV_ACK", "RCV_DATA",
                     "CONNECT", "CLOSE",   "SEND",    "TIMEOUT"   };

struct state_action             // Protocol FSM Structure
{
    void (* action)(void *p);
    enum proto_state next_state;
};

struct packet                   // 504 Byte Packet to & from Simulator
{
    unsigned short type;        // enum packet_type
    unsigned short size;
    char data[MAX_DATA_SIZE];
};

struct p_event                  // Event Structure
{
    enum proto_event event;
    struct packet packet;
    int size;
};

enum proto_state c_state = wait_CON;    // Initial State
volatile int timedout = 0;

static HANDLE hTimer;

static void CALLBACK timer_handler(LPVOID lpArg, DWORD dwTimerLowValue, DWORD dwTimerHighValue)
{
    printf("Timedout\n");
    timedout = 1;
}

static void timer_init(void)
{
    hTimer = CreateWaitableTimer(NULL, FALSE, NULL);
}

static void timer_deinit(void)
{
    CloseHandle(hTimer);
}

void stop_timer()
{
    CancelWaitableTimer(hTimer);
}

void set_timer(int sec)
{
    if (sec <= 0)
    {
        stop_timer();
        return;
    }
    LARGE_INTEGER interval = { 0, };
    interval.QuadPart = -sec * 10000000;
    timedout = 0;
    SetWaitableTimer(hTimer, &interval, 0, timer_handler, NULL, TRUE);
}

void send_packet(int flag, void *p, int size)
{
    packet pkt;
    printf("SEND %s\n", pkt_name[flag]);
    
    pkt.type = flag;
    pkt.size = size;
    if (size)
    {
        memcpy(pkt.data, ((p_event *)p)->packet.data, (size > MAX_DATA_SIZE) ? MAX_DATA_SIZE : size);
    }
    Send((char *)&pkt, sizeof(packet) - MAX_DATA_SIZE + size);
}

static void report_connect(void *p)
{
    stop_timer();
    printf("Connected\n");
}

static void passive_con(void *p)
{
    send_packet(F_ACK, NULL, 0);
    report_connect(NULL);
}

static void active_con(void *p)
{
    send_packet(F_CON, NULL, 0);
    set_timer(CONNECT_TIMEOUT);
}

static void close_con(void *p)
{
    send_packet(F_FIN, NULL, 0);
    printf("Connection Closed\n");
}

static void send_data(void *p)
{
    printf("Send Data to peer '%s' size:%d\n", ((p_event*)p)->packet.data, ((p_event*)p)->size);
    send_packet(F_DATA, (p_event *)p, ((p_event *)p)->size);
}

static void report_data(void *p)
{
    printf("Data Arrived data='%s' size:%d\n", ((p_event*)p)->packet.data, ((p_event*)p)->packet.size);
}

state_action p_FSM[NUM_STATE][NUM_EVENT] =
{
  //  for each event:
  //  RCV_CON,                 RCV_FIN,                 RCV_ACK,                       RCV_DATA,
  //  CONNECT,                 CLOSE,                   SEND,                          TIMEOUT

  // - wait_CON state
  {{ passive_con, CONNECTED }, { NULL, wait_CON },      { NULL, wait_CON },            { NULL, wait_CON },
   { active_con,  CON_sent },  { NULL, wait_CON },      { NULL, wait_CON },            { NULL, wait_CON }},

  // - CON_sent state
  {{ passive_con, CONNECTED }, { close_con, wait_CON }, { report_connect, CONNECTED }, { NULL,      CON_sent },
   { NULL,        CON_sent },  { close_con, wait_CON }, { NULL,           CON_sent },  { active_con, CON_sent }},

  // - CONNECTED state
  {{ passive_con, CONNECTED }, { close_con, wait_CON }, { NULL,      CONNECTED },      { report_data, CONNECTED },
   { NULL,        CONNECTED }, { close_con, wait_CON }, { send_data, CONNECTED },      { NULL,        CONNECTED }},
};

int data_count = 0;

p_event *get_event(void)
{
    static p_event event;    // not thread-safe
    
    while (true)
    {
        // Check if there is user command
        if (!_kbhit())
        {
            // Check if timer is timed-out
            SleepEx(0, TRUE);
            if (timedout)
            {
                timedout = 0;
                event.event = TIMEOUT;
            }
            else
            {
                // Check Packet arrival by event_wait()
                ssize_t n = Recv((char*)&event.packet, sizeof(packet));
                if (n > 0)
                {
                    // if then, decode header to make event
                    switch (event.packet.type)
                    {
                    case F_CON:  event.event = RCV_CON;  break;
                    case F_ACK:  event.event = RCV_ACK;  break;
                    case F_FIN:  event.event = RCV_FIN;  break;
                    case F_DATA:
                        event.event = RCV_DATA; break;
                        event.size = event.packet.size;
                        break;
                    default:
                        continue;
                    }
                }
                else
                    continue;
            }
        }
        else
        {
            int n = _getch();
            switch (n)
            {
            case '0': event.event = CONNECT; break;
            case '1': event.event = CLOSE;   break;
            case '2':
                event.event = SEND;
                sprintf_s(event.packet.data, "%09d", data_count++);
                event.size = strlen(event.packet.data) + 1;
                break;
            case '3': return NULL;  // QUIT
            default:
                continue;
            }
        }
        return &event;
    }
}

void Protocol_Loop(void)
{
    p_event *eventp;

    timer_init();
    while (true)
    {
        printf("Current State = %s\n", st_name[c_state]);

        /* Step 0: Get Input Event */
        if((eventp = get_event()) == NULL)
            break;
        printf("EVENT : %s\n", ev_name[eventp->event]);

        /* Step 1: Do Action */
        if (p_FSM[c_state][eventp->event].action)
            p_FSM[c_state][eventp->event].action(eventp);
        else
            printf("No Action for this event\n");

        /* Step 2: Set Next State */
        c_state = p_FSM[c_state][eventp->event].next_state;
    }
    timer_deinit();
}

int main(int argc, char *argv[])
{
    ChannelNumber channel;
    ID id;
    int rateOfPacketLoss;

    printf("Channel : ");
    scanf_s("%d", &channel);
    printf("ID : ");
    scanf_s("%d", &id);
    printf("Rate of Packet Loss (0 ~ 100)%% : ");
    scanf_s("%d", &rateOfPacketLoss);
    if (rateOfPacketLoss < 0)
        rateOfPacketLoss = 0;
    else if (rateOfPacketLoss > 100)
        rateOfPacketLoss = 100;
        
    // Login to SIMULATOR

    if (Login(channel, id, rateOfPacketLoss) == SOCKET_ERROR)
    {
        printf("Login Failed\n");
        return -1;
    }

    printf("Entering protocol loop...\n");
    printf("type number '[0]CONNECT', '[1]CLOSE', '[2]SEND', or '[3]QUIT'\n");
    Protocol_Loop();

    // SIMULATOR_CLOSE

    return 0;
}

