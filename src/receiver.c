#include "host.h"
#include <assert.h>
#include "switch.h"


void handle_incoming_frames(Host* host) {

    int incoming_frames_length = ll_get_length(host->incoming_frames_head);
    char* messageBuffer = malloc(incoming_frames_length * FRAME_PAYLOAD_SIZE);
    int messageBufferOffset = 0;
    int currPayloadSize = 0;

    while (incoming_frames_length > 0) {

        // printf("Reciever Debug 1 \n");
        LLnode* ll_inmsg_node = ll_pop_node(&host->incoming_frames_head); 
        incoming_frames_length = ll_get_length(host->incoming_frames_head);
        if (ll_inmsg_node == NULL) {
            continue;
        }

        Frame* inframe = ll_inmsg_node->value;

        char* frameToChar = convert_frame_to_char(inframe);
        uint8_t computeCRC = compute_crc8(frameToChar);

        if (computeCRC != 0) {
            ll_destroy_node(ll_inmsg_node);
            printf("Data corrupted in current incoming frame.\n");
            continue;
        }

        // printf("Reciever Debug 2 \n");
        
        // printf("RECIEVER inframe seqNum = %d \n", inframe->seq_num);
        // printf("RECIEVER host LFR = %d \n", host->LFR);
        
        if (inframe->seq_num >= (host->LFR + 1) % MAX_SEQ_NUM) {   
            // printf("Reciever Debug 3 \n");         
            host->LFR = inframe->seq_num;

            if (inframe->remaining_msg_bytes + strlen(inframe->data) > FRAME_PAYLOAD_SIZE) {
                currPayloadSize = FRAME_PAYLOAD_SIZE;
            }
            else {
                currPayloadSize = strlen(inframe->data);
            }

            memcpy(messageBuffer + messageBufferOffset, inframe->data, currPayloadSize);
            messageBufferOffset += currPayloadSize;
            
            // printf("Reciever Debug 4 \n");
            if (inframe->remaining_msg_bytes == 0) {
                // printf("Reciever Debug 4.5 \n");
                messageBuffer[messageBufferOffset] = '\0';
                printf("<RECV_%d>:[%s]\n", host->id, messageBuffer);
                free(messageBuffer);
                messageBuffer = NULL;
                // needed???
                messageBufferOffset = 0;
                host->LFR = -1;
            }

            // printf("Reciever Debug 5 \n");
            Frame* ackFrame = malloc(sizeof(Frame));
            assert(ackFrame);
            ackFrame->src_id = inframe->dst_id;
            ackFrame->dst_id = inframe->src_id;
            ackFrame->seq_num = inframe->seq_num;
            // printf("Reciever Debug 5.1 \n");
            ackFrame->crc_val = 0;
            char* ackFrametoChar = convert_frame_to_char(ackFrame);
            ackFrame->crc_val = compute_crc8(ackFrametoChar);
            // printf("Reciever Debug 5.2 \n");

            Frame* ackFrameCopy = malloc(sizeof(Frame));
            assert(ackFrameCopy);
            memcpy(ackFrameCopy, ackFrame, sizeof(Frame));

            ll_append_node(&host->outgoing_frames_head, ackFrameCopy);
            // printf("Reciever Debug 5.3 \n");
            // printf("Reciever Debug 5.4 \n");
            free(ackFrametoChar);
            // printf("Reciever Debug 5.5 \n");
            free(ackFrame);
            // printf("Reciever Debug 6 \n");
        }

        free(frameToChar);
        free(inframe);
        free(ll_inmsg_node);

        // printf("RECIEVER-Incoming_FrameLength = %d \n", incoming_frames_length);
        // printf("Reciever Debug 7 \n");
    }
}


// void handle_incoming_frames(Host* host) {
//     // TODO: Suggested steps for handling incoming frames
//     //    1) Dequeue the Frame from the host->incoming_frames_head
//     //    2) Compute CRC of incoming frame to know whether it is corrupted
//     //    3) If frame is corrupted, drop it and move on.
//     //    4) Implement logic to check if the expected frame has come
//     //    5) Implement logic to combine payload received from all frames belonging to a message
//     //       and print the final message when all frames belonging to a message have been received.
//     //    6) Implement the cumulative acknowledgement part of the sliding window protocol
//     //    7) Append acknowledgement frames to the outgoing_frames_head queue
//     int incoming_frames_length = ll_get_length(host->incoming_frames_head);
//     while (incoming_frames_length > 0) {
//         // Pop a node off the front of the link list and update the count
//         LLnode* ll_inmsg_node = ll_pop_node(&host->incoming_frames_head);
//         incoming_frames_length = ll_get_length(host->incoming_frames_head);

//         Frame* inframe = ll_inmsg_node->value; 

//         printf("<RECV_%d>:[%s]\n", host->id, inframe->data);

//         free(inframe);
//         free(ll_inmsg_node);
//     }
// }


void run_receivers() {
    int recv_order[glb_num_hosts]; 
    get_rand_seq(glb_num_hosts, recv_order); 

    for (int i = 0; i < glb_num_hosts; i++) {
        glb_hosts_array[i].id = i;
        int recv_id = recv_order[i]; 
        handle_incoming_frames(&glb_hosts_array[recv_id]); 
    }
}