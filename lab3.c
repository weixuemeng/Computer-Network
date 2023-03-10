#include <cnet.h>
#include <cnetsupport.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>

typedef enum { DATA, BEACON, QUERY } FRAMEKIND;

#define    WALKING_SPEED        3.0
#define    PAUSE_TIME        20
#define    BEACON_PERIOD        1000000

#define    TX_NEXT            (5000000 + rand()%5000000)

#define    MAX_MOBILE     15
#define    MAX_ANCHOR     15
#define    MAX_DISTANCE   100
#define    RETRANSMIT_MAX   5

typedef struct {
    int            dest;
    int            src;
    CnetPosition    prevpos;    // position of sender's node
    int            length;        // length of payload
    FRAMEKIND      kind;
    int            times;
} WLAN_HEADER;

typedef struct {
    WLAN_HEADER        header;
    char        payload[2304];
    CnetPosition   anchorPos;  // beacon_msg
} WLAN_FRAME;

typedef struct {
    WLAN_FRAME     buffer[20]; //data packets for delivering
    CnetPosition    anchorPos;
    CnetAddr       anchorAddr; // anchor address
    int            capacityAva; // frame slot available for anchor to store data
    int            distanceAva; // maximum distance in order to set a connection between sender and anchor ( meter)
}ANCHOR;



static    int        *stats        = NULL;
static    CnetPosition    mobileNow;
static    CnetPosition    anchorNow;
int    messageId = 0;


static    bool        verbose        = false;

int  mobileNum;       // number of mobile node
int mobile_addr[15];  // list mobile address
ANCHOR   anchors[15]; // list of ANCHOR
int bufferIndex = 0;  // next buffer position

/* ----------------------------------------------------------------------- */

static EVENT_HANDLER(transmit)
{
    WLAN_FRAME    frame;
    int        link    = 1;
    CnetPosition    defaultPos = { -1, -1, 0 };
    
    
    //POPULATE A NEW FRAME ( do not send to anchor)
    if (nodeinfo.nodetype == NT_MOBILE){ // mobile node ( no generate packet to anchor)
        CHECK(CNET_get_position(&mobileNow, NULL));
        //printf("mobile x = %f, y = %f\n", mobileNow.x, mobileNow.y);
        do {
            int randomIndex = rand() % (mobileNum-1);// random index from 0-4 ( 5-1)
            frame.header.dest = mobile_addr[ randomIndex];
        } while((frame.header.dest == nodeinfo.address));
        frame.header.kind       = DATA;
        frame.header.src        = nodeinfo.address;               // eg: m ( generate message)
        frame.header.prevpos    = mobileNow;                       // eg: m's position

        sprintf(frame.payload, "hello from %d message %d", nodeinfo.address, ++messageId);
        frame.header.length    = strlen(frame.payload) + 1;    // send nul-byte too
        
        frame.anchorPos = defaultPos;

        //TRANSMIT THE FRAME
        size_t len    = sizeof(WLAN_HEADER) + frame.header.length;
        CHECK(CNET_write_physical_reliable(link, &frame, &len));
        
        ++stats[0];
        
//TEST!
        printf("mobile [ %d ]( x = %.3f, y = %.3f): pkt transmitted (src = %d, dest = %d)\n", nodeinfo.address, frame.header.prevpos.x, frame.header.prevpos.y,frame.header.src,frame.header.dest);
        printf("packet transmitted : %s\n", frame.payload);
        printf("\n");

        if(verbose) {
        fprintf(stdout, "\n%d: transmitting '%s' to %d\n",
                nodeinfo.address, frame.payload, frame.header.dest);
        }

        //SCHEDULE OUR NEXT TRANSMISSION
        CNET_start_timer(EV_TIMER1, TX_NEXT, 0);
    }
}

static EVENT_HANDLER(beacon_msg_send){
    WLAN_FRAME    frame;
    int        link    = 1;
    
    // anchor node ( no generate packet to mobile)
    if (nodeinfo.nodetype == NT_ACCESSPOINT){
        
        // TODO: get anchor position
        CHECK(CNET_get_position(&anchorNow, NULL));
        //printf("In beacon_msg: anchor position x = %f, y = %f\n", anchorNow.x, anchorNow.y);
        
        // TODO: send beacon_msg
        //for ( int i = 0; i< 2; i++){  // ( 0-14, could change to 2 -> only has 2 anchors)

        frame.header.kind = BEACON;
        frame.header.src  = nodeinfo.address; // actrally no need, but can stay here
        frame.header.dest = 500; // same for everyone
        
        frame.header.prevpos = anchorNow;
        frame.anchorPos = anchorNow; // BEACON : indicate anchor position NEEDED
        printf("beacon number %d from %d\n", ++messageId, nodeinfo.address);
        
        sprintf(frame.payload, "beacon number %d from %d",messageId, nodeinfo.address); //change data
        frame.header.length = strlen(frame.payload)+1;
        
        //TRANSMIT THE BEACON FRAME
        size_t len    = sizeof(WLAN_HEADER) + frame.header.length;
        CHECK(CNET_write_physical_reliable(link, &frame, &len));
        
        CNET_start_timer(EV_TIMER2, BEACON_PERIOD, 0);

    }
}

//  DETERMINE 2D-DISTANCE BETWEEN TWO POSITIONS
static double distance(CnetPosition p0, CnetPosition p1)
{
    int    dx    = p1.x - p0.x;
    int    dy    = p1.y - p0.y;

    return sqrt(dx*dx + dy*dy);
}

//  INITIALIZE MOBILE-LIST
static void init_mobile(){
    char *mobileStr = "mobiles";
    char *mobile_char_addr= CNET_getvar(mobileStr); // get char array from topology file
    const char s[3] = ", ";
    char *token = strtok(mobile_char_addr, s);
    mobileNum = 0;
    while (token != NULL){
        mobile_addr[mobileNum] = atoi(token);
        token = strtok(NULL, s);
        mobileNum++; // 5
    }
}

//  INITIALIZE ANCHOR-LIST ( MOBILE USE TO CHECK IF THEY COULD RETRANSMIT TO ANCHOR)
static void init_anchor(){ // leave anchor position tobe empty
    char *anchorStr = "anchors";
    char *anchor_char_addr= CNET_getvar(anchorStr); // get char array from topology file
    const char s[3] = ", ";
    char *token2 = strtok(anchor_char_addr, s);
    int anchorIndex = 0;
    CnetPosition    defaultPos = { -1, -1, 0 };

    while (token2 != NULL){
        ANCHOR anchor;
        anchor.anchorAddr= atoi(token2);
        anchor.capacityAva = 20;
        anchor.distanceAva = 50;
        
        // TODO: initialize anchor position ( NULL)
        anchor.anchorPos = defaultPos; // 还是用CNET_set_position()
        
        anchors[anchorIndex]= anchor;
        token2 = strtok(NULL, s);
        anchorIndex++; // 2
    }
}
//  Test mobile Address List
static void testMobileList(){
    printf(" mobile address are: \n");
    for ( int i = 0; i<mobileNum; i++){
        printf("  %d\n", mobile_addr[i]);
    }
}

//  TEST ANCHOR LIST
static void testAnchorList(){
    for ( int i = 0; i<2; i++){
        printf("anchor Address = %d\n",anchors[i].anchorAddr);
        printf("anchor capacity = %d\n",anchors[i].capacityAva);
        printf("anchor distance limit = %d\n",anchors[i].distanceAva);
        printf("anchor anchorPos x = %.3f, y = %.3f\n",anchors[i].anchorPos.x, anchors[i].anchorPos.y);
    }
}


static EVENT_HANDLER(receive)
{
    WLAN_FRAME    frame;
    size_t    len;
    int        link;
    int        index; // which anchor receive retransmit packet

    CnetPosition    defaultPos = { -1, -1, 0};


//  READ THE ARRIVING FRAME FROM OUR PHYSICAL LINK
    len    = sizeof(frame);
    CHECK(CNET_read_physical(&link, &frame, &len));
    if(verbose) {
    double    rx_signal;
    CHECK(CNET_wlan_arrival(link, &rx_signal, NULL));
    fprintf(stdout, "\t%5d: received '%s' @%.3fdBm\n",
                nodeinfo.address, frame.payload, rx_signal);
    }
    
// ANCHOR NODE
    if (nodeinfo.nodetype == NT_ACCESSPOINT){
        //  Get the current location of the node (anchor)
        CHECK(CNET_get_position(&anchorNow, NULL));
        
        // TODO: find anchor or initilize anchor in list
        for ( int i=0;i < MAX_ANCHOR; i++){  // check which anchor
            if (nodeinfo.address == anchors[i].anchorAddr){
                index = i;     // anchor in index i
                break;
            }
        }

        // TODO: IF frame.kind = QUERY:
        if ( frame.header.kind == QUERY){
            printf("anchor [%d]: download request (dest = %d)\n", nodeinfo.address, frame.header.src);
            
            // TODO: CHECK if anchor has stored message query from mobile ( eg: y)
            for ( int i = 0; i<= bufferIndex-1; i++){
                if ( frame.header.src == anchors[index].buffer[i].header.dest){ // has data stored for dest(make query) y==y
                    frame = anchors[index].buffer[i];
                    
                    printf("anchor [%d]: download reply (src = %d, dest = %d)\n", nodeinfo.address, anchors[index].buffer[i].header.src, anchors[index].buffer[i].header.dest);
                    
                    // TODO: DELETE !!!
                    for ( int i= index; i<= bufferIndex-1; i++){
                        anchors[index].buffer[i]= anchors[index].buffer[i+1];
                    }
                    anchors[index].capacityAva++;
                    bufferIndex--;
                    
                    len            = sizeof(WLAN_HEADER) + frame.header.length;
                    CHECK(CNET_write_physical_reliable(link, &frame, &len)); // send to y
                    
                }
            }
        }
        
        // TODO: IF frame.kind = DATA:
        else if ( frame.header.kind == DATA){
            printf("anchor [%d] receive a packet (src = %d, dest = %d)\n",index, frame.header.src, frame.header.dest);
            
            //TODO: check if the data has stored before

            printf("anchor [%d] capacity: %d\n", index, anchors[index].capacityAva);
            
            // TODO: check capacity
            bool couldBuffer = false;
            if (anchors[index].capacityAva>0)
                couldBuffer = true;
            
            // TODO: buffer the data packet if not exceed capacity
            if ( couldBuffer == true){
                anchors[index].buffer[bufferIndex] = frame;
                anchors[index].capacityAva--;
                bufferIndex++;
                printf("anchor [%d]: pkt received and stored (src = %d, dest = %d)\n", nodeinfo.address, frame.header.src, frame.header.dest);
            }else{
                printf("Anchor buffer is full!\n");
            }
//            }
        }
    }
    
// MOBILE NODE
    else if (nodeinfo.nodetype == NT_MOBILE){
        //  Get the current location of the node (anchor)
        CHECK(CNET_get_position(&mobileNow, NULL));
        
        // TODO: IF frame.kind = BEACON :
        if ( frame.header.kind == BEACON){  // eg: beacon_msg -> y / x
//TEST
            printf("mobile [%d] (x = %.3f, y = %.3f): beacon received ( src = %d )\n", nodeinfo.address,mobileNow.x, mobileNow.y, frame.header.src);
            CnetPosition anchorPos;
            anchorPos.x =frame.header.prevpos.x; // z's ( anchor) position (stored in beacon_msg)
            anchorPos.y =frame.header.prevpos.y;
            
            //CnetPosition anchorPos = frame.header.prevpos; // z's ( anchor) position (stored in beacon_msg)
            
            // TODO: update anchors list
            for ( int i = 0; i< 2; i++){
                if (anchors[i].anchorAddr == frame.header.src){ // from which anchor I received
// TEST:
//                    printf("anchors[%d].anchorPos.x = %f, defaultPos = %f",i, anchors[i].anchorPos.x,defaultPos.x);
//                    printf("anchors[%d].anchorPos.y = %f, defaultPos = %f",i, anchors[i].anchorPos.y,defaultPos.y);
//                    printf("anchors[%d].anchorPos.z = %f, defaultPos = %f",i, anchors[i].anchorPos.z,defaultPos.z);
                    
                    printf("anchorPos[%d] = (x = %.3f, y = %.3f), mobile: (x = %.3f, y= %.3f): check distance when query = %.3f m\n",i,anchorPos.x, anchorPos.y, mobileNow.x, mobileNow.y,distance(mobileNow,anchorPos));
                    printf(" beacon received content = %s\n", frame.payload);
                    printf("\n");

                    if(( anchors[i].anchorPos.x == defaultPos.x)){
                        anchors[i].anchorPos.x = anchorPos.x; // update the anchor position in the anchors list
                        anchors[i].anchorPos.y = anchorPos.y; // update the anchor position in the anchors list
                    }

                }
            }
            
            if (distance ( mobileNow, anchorPos)< MAX_DISTANCE){ // < 100 ( y could make a query)
                printf("Make a query! \n");
                // TODO: make a query
                frame.header.kind = QUERY;
                frame.header.dest = frame.header.src; // dest = z (anchor)
                frame.header.src  = nodeinfo.address; // src  = x,y,m (mobile)
                frame.header.prevpos = mobileNow;
                sprintf(frame.payload, "query from %d", nodeinfo.address); //change data( no need I think)
                
                frame.header.length    = strlen(frame.payload) + 1;
                len            = sizeof(WLAN_HEADER) + frame.header.length;
                CHECK(CNET_write_physical_reliable(link, &frame, &len));
            }
        }
        
        // TODO: IF frmae.kind = DATA :
        else if (frame.header.kind == DATA){
            //  IS THIS FRAME FOR ME?
            if(frame.header.dest == nodeinfo.address) { // ARRIVE !!  pkt -> y
                printf("frame.header.kind = %d\n",frame.header.kind);
                ++stats[1];
//TEST
                printf("mobile [%d]( x = %.3f, y = %.3f): pkt received (src = %d, dest = %d)\n", nodeinfo.address, mobileNow.x, mobileNow.y,frame.header.src,frame.header.dest);
                printf("received packet : %s\n", frame.payload);
                printf("\n");
                
            }
            // NO; RETRANSMIT FRAME IF WE'RE CLOSER TO THE DESTINATION THAN THE PREV NODE  pkt -> x ---> anchor z/z' --->y
            else {
//TEST
                printf("mobile [ %d ]( x = %.3f, y = %.3f): pkt relayed (src = %d, dest = %d)\n", nodeinfo.address, mobileNow.x, mobileNow.y,frame.header.src,frame.header.dest);
                printf("received packet : %s\n", frame.payload);

                
                // TODO: check if x is close to z ( can I find the anchor ?)
                CnetPosition  anchorPos;             // z's (anchor) position
                CnetPosition  lastMobilePos;   // previous mobile node position
                for ( int i=0; i< 2; i++){
                    if(( anchors[i].anchorPos.x == defaultPos.x)){
                        printf("There is no anchor address you can find from anchor [%d]\n",i);
                        continue;  // no retransmit ( can't find anchor's position)
                    }
                    else{ // can find an anchor's position
                        // TODO: retransmit to anchor if possiable
                        anchorPos.x = anchors[i].anchorPos.x; // z1's position/  z2's position
                        anchorPos.y = anchors[i].anchorPos.y; // z1's position/  z2's position
                        
                        lastMobilePos.x = frame.header.prevpos.x;
                        lastMobilePos.y = frame.header.prevpos.y;
                        
// TEST
                        printf("anchorPos[%d] = (x = %.3f, y = %.3f), mobile: (x = %.3f, y= %.3f): check distance when retransmit = %.3f m\n",i,anchorPos.x, anchorPos.y, mobileNow.x, mobileNow.y,distance(mobileNow,anchorPos));
                        
                        if (distance(mobileNow, anchorPos)<distance(lastMobilePos, anchorPos) &&( distance(mobileNow, anchorPos) < MAX_DISTANCE)){  // <150 ( can transmit to the anchor)
                            printf("retransmit\n");
                            printf("\n");
                            
                            // ONLY change prevPos
                            frame.header.prevpos = mobileNow;
                            //frame.header.times++;
                            
                            len            = sizeof(WLAN_HEADER) + frame.header.length;
                            CHECK(CNET_write_physical_reliable(link, &frame, &len));
                            if(verbose)
                            fprintf(stdout, "\t\tretransmitting\n");
                            
                        }
                    }
                }
            }
        }
    }
}

//  THIS FUNCTION IS CALLED ONCE, ON TERMINATION, TO REPORT OUR STATISTICS
static EVENT_HANDLER(finished)
{
    
    fprintf(stdout, "messages generated:\t%d\n", stats[0]);
    fprintf(stdout, "messages received:\t%d\n", stats[1]);
    if(stats[0] > 0)
    fprintf(stdout, "delivery ratio:\t\t%.1f%%\n", 100.0*stats[1]/stats[0]);
}

static EVENT_HANDLER(button_pressed){
    if( nodeinfo.nodetype == NT_MOBILE)
        testMobileList();
    if(nodeinfo.nodetype == NT_ACCESSPOINT)
        testAnchorList();
    
}

EVENT_HANDLER(reboot_node)
{
    extern void init_mobility(double walkspeed_m_per_sec, int pausetime_secs);
    // extern CnetPosition position; use CNET_getPosition()

    if(NNODES == 0) {
    fprintf(stderr, "simulation must be invoked with the -N switch\n");
    exit(EXIT_FAILURE);
    }

//  ENSURE THAT WE'RE USING THE CORRECT VERSION OF cnet
    CNET_check_version(CNET_VERSION);
    srand(time(NULL) + nodeinfo.nodenumber);

//  INITIALIZE MOBILITY PARAMETERS
    if ( nodeinfo.nodetype == NT_MOBILE)
        init_mobility(WALKING_SPEED, PAUSE_TIME);
    init_mobile();
    init_anchor();
    
//  ALLOCATE MEMORY FOR SHARED MEMORY SEGMENTS
    stats    = CNET_shmem2("s", 2*sizeof(int));
    //positions    = CNET_shmem2("p", NNODES*sizeof(CnetPosition)); // can't use in this part!
    
//  PREPARE FOR SENDING PERIODIC BEACON MSG
    if ( nodeinfo.nodetype == NT_ACCESSPOINT)
        CHECK(CNET_set_handler(EV_TIMER2, beacon_msg_send , 0));
    CNET_start_timer(EV_TIMER2, BEACON_PERIOD, 0);


//  PREPARE FOR OUR MESSAGE GENERATION AND TRANSMISSION
    CHECK(CNET_set_handler(EV_TIMER1, transmit, 0));
    CNET_start_timer(EV_TIMER1, TX_NEXT, 0);
    

    
// BUTTON PRESSED
    CNET_set_handler(EV_DEBUG0, button_pressed,0);
    CNET_set_debug_string(EV_DEBUG0, "Node Info");

//  SET HANDLERS FOR EVENTS FROM THE PHYSICAL LAYER
    CHECK(CNET_set_handler(EV_PHYSICALREADY,  receive, 0));

//  WHEN THE SIMULATION TERMINATES, NODE-0 WILL REPORT THE STATISTICS
    if(nodeinfo.nodenumber == 0)
    CHECK(CNET_set_handler(EV_SHUTDOWN,  finished, 0));
}

