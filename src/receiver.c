#include "host.h"
#include <assert.h>
#include "switch.h"

// Send ack to sender when called upon.
void send_ack(Host* host, int ack_num, Frame* frame) {
    // printf("send_ack 1 src id = %d \n", frame->src_id);
    // printf("send_ack 1 dst id = %d \n", frame->dst_id);
    Frame* ackFrame = malloc(sizeof(Frame));
    assert(ackFrame);
    // printf("send_ack 2 src id = %d \n", frame->src_id);
    // printf("send_ack 2 dst id = %d \n", frame->dst_id);
    uint8_t srcID = frame->dst_id;
    uint8_t dstID = frame->src_id;
    ackFrame->src_id = srcID;
    ackFrame->dst_id = dstID;
    // printf("send_ack 3 src id = %d \n", dstID);
    // printf("send_ack 3 dst id = %d \n", srcID);
    
    ackFrame->seq_num = ack_num;
    ackFrame->crc_val = 0;
    char* ackFrameToChar = convert_frame_to_char(ackFrame);
    ackFrame->crc_val = compute_crc8(ackFrameToChar);
    // printf("a \n");
    ll_append_node(&host->outgoing_frames_head, ackFrame);
    // printf("b \n");
    free(ackFrameToChar);
    // printf("c \n");
}


void handle_incoming_frames(Host* host) {
    
    int incoming_frames_length = ll_get_length(host->incoming_frames_head);
    
    // While therea are still frames in queue
    while (incoming_frames_length > 0) {

        // printf("yup 3 length = %d\n", incoming_frames_length);

        // printf("incoming frames length = %d \n", incoming_frames_length);

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
            // ! Here \/ needed null?
            // poppedFrame = NULL;
            // printf("Data corrupted in incoming frame %d.\n", poppedFrame->seq_num);
           // ll_destroy_node(poppedNode);
            continue;
        } 

        // If poppedNode/poppedFrame is NOT corrupted, enter here.
        else {

            uint8_t seq_num = poppedFrame->seq_num;
            int senderSrcId = poppedFrame->src_id;
            RecieverState* reciever = &host->recieverStructure[senderSrcId];

            // ! initalize && if frame does not already exist in FrameArray i.e. not already processed           
            int wrapAround = seq_num_diff(reciever->LFR, seq_num);
            int wrapAround1 = seq_num_diff(reciever->LFR ,reciever->LAF);

            if (wrapAround > 0 && wrapAround1 <= glb_sysconfig.window_size)  {

                enqueue(host->queue, poppedFrame->seq_num, poppedFrame);

                Node* minNode = getMin(host->queue);
                
                while ( !isEmpty(host->queue) && (reciever->LFR + 1) % 256 <= minNode->seqNum /* && found == 0*/ ) {
                    
                    Node* NodeFromQueue = popMin(host->queue);
                    Frame* FrameFromMinQueue = NodeFromQueue->frame;

                    // printf("minQueueFrame SRC = %d\n", FrameFromMinQueue->src_id);
                    // printf("minQueueFrame DST = %d\n", FrameFromMinQueue->dst_id);

                    strcat(reciever->messageBuffer, FrameFromMinQueue->data);

                    if (FrameFromMinQueue->remaining_msg_bytes == 0) {
                        printf("<RECV_%d>:[%s]\n", host->id, reciever->messageBuffer);
                        reciever->messageBuffer[0] = '\0';
                        // ! added this, is it ok?
                        clearMinQueue(host->queue);
                        
                    }

                    // Advance LFR
                    // reciever->LFR = (reciever->LFR + 1) % 256;
                    // printf("--------------------------------------- \n");
                    printf("RECIEVER - LFR = %d \n", reciever->LFR);
                    printf("RECIEVER - Seq = %d \n", FrameFromMinQueue->seq_num);
                    printf("--------------------------------------- \n");
                    reciever->LFR = FrameFromMinQueue->seq_num;
                    free(FrameFromMinQueue);
                    FrameFromMinQueue = NULL;
                    
                }

                // Send a cumulative acl for the last frame processed

                // printf("poppedFrame 2 SRC = %d\n", poppedFrame->src_id);
                // printf("poppedFrame 2 DST = %d\n", poppedFrame->dst_id);
                reciever->LAF = reciever->LFR + glb_sysconfig.window_size;
                send_ack(host, reciever->LFR, poppedFrame);
                // free(poppedFrame);
            }

            // if not within window, drop the frame.
            else {
                // printf("Frame %d is outside the window \n", poppedFrame->seq_num);
                // send_ack(host, reciever->LFR, poppedFrame);
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