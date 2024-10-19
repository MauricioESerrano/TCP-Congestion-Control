#include "host.h"
#include <assert.h>
#include "switch.h"

// Send ack to sender when called upon.
void send_ack(Host* host, int ack_num, Frame* frame) {
    Frame* ackFrame = malloc(sizeof(Frame));
    assert(ackFrame);
    uint8_t srcID = frame->dst_id;
    uint8_t dstID = frame->src_id;
    ackFrame->src_id = srcID;
    ackFrame->dst_id = dstID;
    ackFrame->seq_num = ack_num;
    ackFrame->crc_val = 0;
    char* ackFrameToChar = convert_frame_to_char(ackFrame);
    ackFrame->crc_val = compute_crc8(ackFrameToChar);
    ll_append_node(&host->outgoing_frames_head, ackFrame);
    free(ackFrameToChar);
}


void handle_incoming_frames(Host* host) {
    
    int incoming_frames_length = ll_get_length(host->incoming_frames_head);
    
    // While therea are still frames in queue
    while (incoming_frames_length > 0) {

        LLnode* poppedNode = ll_pop_node(&host->incoming_frames_head);
        incoming_frames_length = ll_get_length(host->incoming_frames_head);

        if (poppedNode == NULL) {
            continue;
        }

        // Convert poppedNode to Frame
        Frame* poppedFrame = poppedNode->value;

        // Compute CRC
        char* poppedFrameToChar = convert_frame_to_char(poppedFrame);
        uint8_t computeCRC = compute_crc8(poppedFrameToChar);
        free(poppedFrameToChar);

        // Use CRC and Check for Corruption, if poppedFrame corrupted, Destroy and continue to next iteration in incoming frames head.
        if (computeCRC != 0) {

            // printf("RECIEVER - Data corrupted in incoming frame %d.\n", poppedFrame->seq_num);
           // ll_destroy_node(poppedNode);
            continue;
        } 


        else {

            // printf("RECIEVER - FRAME RECIEVED \n");

            uint8_t seq_num = poppedFrame->seq_num;
            int senderSrcId = poppedFrame->src_id;
            RecieverState* reciever = &host->recieverStructure[senderSrcId];

            // ! initalize && if frame does not already exist in FrameArray i.e. not already processed           
            int wrapAround = seq_num_diff(reciever->LFR, seq_num);
            int wrapAround1 = seq_num_diff(reciever->LFR ,reciever->LAF);

            int diff = seq_num_diff(seq_num, reciever->LFR);

            if (wrapAround > 0 && wrapAround1 <= glb_sysconfig.window_size)  {

                enqueue(host->queue, poppedFrame->seq_num, poppedFrame);
                Node* minNode = getMin(host->queue);
                
                while ( !isEmpty(host->queue) && (reciever->LFR + 1) % 256 <= minNode->seqNum) {
                    
                    Node* NodeFromQueue = popMin(host->queue);
                    Frame* FrameFromMinQueue = NodeFromQueue->frame;

                    strcat(reciever->messageBuffer, FrameFromMinQueue->data);

                    if (FrameFromMinQueue->remaining_msg_bytes == 0) {
                        // printf("--------------------------------------- \n");
                        printf("<RECV_%d>:[%s]\n", host->id, reciever->messageBuffer);
                        //  printf("--------------------------------------- \n");
                        reciever->messageBuffer[0] = '\0';
                        // ! added this, is it ok?
                        clearMinQueue(host->queue);   
                    }

                    reciever->LFR = FrameFromMinQueue->seq_num;
                    free(FrameFromMinQueue);
                    FrameFromMinQueue = NULL;   
                }

                reciever->LAF = reciever->LFR + glb_sysconfig.window_size;
                send_ack(host, reciever->LFR, poppedFrame);
                // free(poppedFrame);
            }

            if (diff <= 0) {

                send_ack(host, reciever->LFR, poppedFrame);

            }
        }
    }
}

void run_receivers() {
    int recv_order[glb_num_hosts]; 
    get_rand_seq(glb_num_hosts, recv_order); 

    for (int i = 0; i < glb_num_hosts; i++) {
        glb_hosts_array[i].id = i;
        int recv_id = recv_order[i]; 
        handle_incoming_frames(&glb_hosts_array[recv_id]); 
    }
}