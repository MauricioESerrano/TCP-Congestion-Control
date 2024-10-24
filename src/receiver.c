#include "host.h"
#include <assert.h>
#include "switch.h"

// Send ack to sender when called upon.
void send_ack(Host* host, uint8_t ack_num, Frame* poppedFrame) {
    Frame* ackFrame = malloc(sizeof(Frame));
    assert(ackFrame);
    ackFrame->src_id = host->id;
    ackFrame->dst_id = poppedFrame->src_id;
    ackFrame->seq_num = ack_num;
    ackFrame->crc_val = 0;
    char* ackFrameToChar = convert_frame_to_char(ackFrame);
    ackFrame->crc_val = compute_crc8(ackFrameToChar);
    free(ackFrameToChar);
    ll_append_node(&host->outgoing_frames_head, ackFrame);
}

void handle_incoming_frames(Host* host) {
    int incoming_frames_length = ll_get_length(host->incoming_frames_head);
    while (incoming_frames_length > 0) {
        LLnode* poppedNode = ll_pop_node(&host->incoming_frames_head);
        incoming_frames_length = ll_get_length(host->incoming_frames_head);
        if (poppedNode == NULL) {
            continue;
        }
        Frame* poppedFrame = poppedNode->value;
        // Compute CRC
        char* poppedFrameToChar = convert_frame_to_char(poppedFrame);
        uint8_t computeCRC = compute_crc8(poppedFrameToChar);
        free(poppedFrameToChar);

        // Use CRC and Check for Corruption, if poppedFrame corrupted, Destroy and continue to next iteration in incoming frames head.
        if (computeCRC != 0) {
            continue;
        } 
        int senderSrcId = poppedFrame->src_id;
        RecieverState* reciever = &host->recieverStructure[senderSrcId];

        // initalize && if frame does not already exist in FrameArray i.e. not already processed           
        if ( seq_num_diff(poppedFrame->seq_num, reciever->LFR) < 0 && seq_num_diff(poppedFrame->seq_num, reciever->LAF) >= 0) {
            enqueue(host->arrayMinQueue->minQueues[senderSrcId], poppedFrame->seq_num, poppedFrame);
            Node* minNode = getMin(host->arrayMinQueue->minQueues[senderSrcId]);

            // minnode doesnt get updated after first loop because its outside the loop
            while (minNode != NULL && seq_num_diff(reciever->LFR, minNode->seqNum) == 1) {
                Node* NodeFromQueue = popMin(host->arrayMinQueue->minQueues[senderSrcId]);
                Frame* FrameFromMinQueue = NodeFromQueue->frame;
                strcat(reciever->messageBuffer, FrameFromMinQueue->data);

                // logical flaw due to inorder
                if (FrameFromMinQueue->remaining_msg_bytes == 0) {
                    printf("<RECV_%d>:[%s]\n", host->id, reciever->messageBuffer);
                    reciever->messageBuffer[0] = '\0';
                    clearMinQueue(host->arrayMinQueue->minQueues[senderSrcId]);   
                }
                minNode = getMin(host->arrayMinQueue->minQueues[senderSrcId]);
                reciever->LFR = FrameFromMinQueue->seq_num;
                reciever->LAF = reciever->LFR + glb_sysconfig.window_size;
                send_ack(host, reciever->LFR, FrameFromMinQueue);
                FrameFromMinQueue = NULL;
            }
        }
        else {
            send_ack(host, reciever->LFR, poppedFrame);
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