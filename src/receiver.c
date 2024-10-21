#include "host.h"
#include <assert.h>
#include "switch.h"

// Send ack to sender when called upon.
void send_ack(Host* host, uint8_t ack_num, Frame* poppedFrame) {
    // printf("here send_ack \n");
    Frame* ackFrame = malloc(sizeof(Frame));
    assert(ackFrame);
    // printf("frame dst id = %d \n", frame->dst_id);
    // printf("frame src id = %d \n", frame->src_id);
    // uint8_t srcID = frame->dst_id;
    // uint8_t dstID = frame->src_id;
    ackFrame->src_id = host->id;
    ackFrame->dst_id = poppedFrame->src_id;
    // printf("ack dst id = %d \n", ackFrame->dst_id);
    // printf("ack src id = %d \n", ackFrame->src_id);
    ackFrame->seq_num = ack_num;
    ackFrame->crc_val = 0;
    char* ackFrameToChar = convert_frame_to_char(ackFrame);
    ackFrame->crc_val = compute_crc8(ackFrameToChar);
    free(ackFrameToChar);
    ll_append_node(&host->outgoing_frames_head, ackFrame);
}


void handle_incoming_frames(Host* host) {

    int incoming_frames_length = ll_get_length(host->incoming_frames_head);
    
    // While therea are still frames in queue
    while (incoming_frames_length > 0) {

        // printf("length = %d \n", incoming_frames_length);

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
            // printf("RECIEVER - data corrupted Frame %d \n", poppedFrame->seq_num);
           // ll_destroy_node(poppedNode);
            continue;
        } 


        else {

            uint8_t seq_num = poppedFrame->seq_num;
            int senderSrcId = poppedFrame->src_id;
            RecieverState* reciever = &host->recieverStructure[senderSrcId];



            // // ! added ----------------------------------------------

            // Frame* ackFrame = malloc(sizeof(Frame));
            // assert(ackFrame);
            // uint8_t srcID = poppedFrame->dst_id;
            // uint8_t dstID = poppedFrame->src_id;
            // ackFrame->src_id = srcID;
            // ackFrame->dst_id = dstID;
            // ackFrame->crc_val = 0;

            // char* ackFrameToChar = convert_frame_to_char(ackFrame);
            // ackFrame->crc_val = compute_crc8(ackFrameToChar);

            // free(ackFrameToChar);


            // // !-----------------------------------------------------

            // ! initalize && if frame does not already exist in FrameArray i.e. not already processed           
            int wrapAround = seq_num_diff(reciever->LFR, seq_num);
            int wrapAround1 = seq_num_diff(reciever->LFR ,reciever->LAF);

            int diff = seq_num_diff(seq_num, reciever->LFR);
            // is this current frame within the window
            if (wrapAround > 0 && wrapAround1 <= glb_sysconfig.window_size)  {

                enqueue(host->queue, poppedFrame->seq_num, poppedFrame);
                Node* minNode = getMin(host->queue);


                // logically, == not <= 
                while ( !isEmpty(host->queue) && (reciever->LFR + 1) % 256 <= minNode->seqNum) {
                    
                    Node* NodeFromQueue = popMin(host->queue);
                    Frame* FrameFromMinQueue = NodeFromQueue->frame;

                    strcat(reciever->messageBuffer, FrameFromMinQueue->data);

                    // printf("string data = %s \n", FrameFromMinQueue->data);

                    if (FrameFromMinQueue->remaining_msg_bytes == 0) {

                        printf("<RECV_%d>:[%s]\n", host->id, reciever->messageBuffer);
                        reciever->messageBuffer[0] = '\0';
                        // ! added this, is it ok?
                        clearMinQueue(host->queue);   
                    }

                    reciever->LFR = FrameFromMinQueue->seq_num;
                    // free(FrameFromMinQueue);
                    FrameFromMinQueue = NULL;   
                }

                reciever->LAF = reciever->LFR + glb_sysconfig.window_size;
                send_ack(host, reciever->LFR, poppedFrame);
                // printf("src id = %d \n", srcID);
                // printf("dst id = %d \n", dstID);

                // send_ack(host, reciever->LFR, srcID, dstID);
                // free(poppedFrame);
            }
            // not in window, was it before the frame? if so, send ack 
            if (diff <= 0) {

                send_ack(host, reciever->LFR, poppedFrame);
                // send_ack(host, reciever->LFR, srcID, dstID);

            }
        }
        // julio yes sur
        // free(poppedFrame);
        // free(poppedNode);
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