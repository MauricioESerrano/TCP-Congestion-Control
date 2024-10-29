#include "host.h"
#include <assert.h>
#include "switch.h"

void shiftLeft(Host* host) {
    int j = 0;

    // Move non-NULL slots to the beginning of the array
    for (int i = 0; i < glb_sysconfig.window_size; i++) {
        if (host->send_window[i].frame != NULL) {
            host->send_window[j++] = host->send_window[i];
        }
    }

    // Set the remaining slots to NULL
    while (j < glb_sysconfig.window_size) {
        host->send_window[j].frame = NULL;
        host->send_window[j].timeout = NULL;
        j++;
    }
}

void FastRetransmission(Host* host, Frame* frameReference, struct timeval curr_timeval) {

    uint8_t senderId = frameReference->src_id;
    RecieverState* reciever = &host->recieverStructure[senderId];
    CongestionControl* cc = &host->cc[senderId];
    uint8_t ftFrameSeq = (uint8_t) reciever->LAR + 1;

    long additional_ts = 0; 

    if (timeval_usecdiff(&curr_timeval, host->latest_timeout) > 0) {
        memcpy(&curr_timeval, host->latest_timeout, sizeof(struct timeval)); 
    }

    // find the frame we want to retransmit
    for (int i = 0; i < glb_sysconfig.window_size; i++) {
        // only fast retransmit the last acknowledged frame + 1, then break.
        
        if (reciever->numSent < (int) cc->cwnd) {

            if (host->send_window[i].frame != NULL && ftFrameSeq == host->send_window[i].frame->seq_num) {

                Frame* outgoingFrame = host->send_window[i].frame;
                Frame* copyOfOutgoingFrame = malloc(sizeof(Frame));
                assert(copyOfOutgoingFrame);
                memcpy(copyOfOutgoingFrame, outgoingFrame, sizeof(Frame));
                ll_append_node(&host->outgoing_frames_head, copyOfOutgoingFrame);
                struct timeval* next_timeout = malloc(sizeof(struct timeval));
                memcpy(next_timeout, &curr_timeval, sizeof(struct timeval)); 
                timeval_usecplus(next_timeout, TIMEOUT_INTERVAL_USEC);
                host->send_window[i].timeout = next_timeout;
                break;

            }
        }
    }

    memcpy(host->latest_timeout, &curr_timeval, sizeof(struct timeval)); 
    timeval_usecplus(host->latest_timeout, additional_ts);
}

void TCPCongestionControl(Host* host, Frame* ackFrame, struct timeval curr_timeval) {

    // Extract sender ID from the acknowledgment frame
    uint8_t senderId = ackFrame->src_id;
    RecieverState* reciever = &host->recieverStructure[senderId];
    CongestionControl* cc = &host->cc[senderId];

    // -----------------------------------------------Slow Start State-------------------------------------------------------------

    // Slow Start (SS) state
    if (cc->state == cc_SS) {

        cc->dup_acks = (seq_num_diff(reciever->LAR, ackFrame->seq_num) <= 0) ? (cc->dup_acks + 1) : 0;

        // If 3 duplicate ACKs, initiate fast retransmission
        if (cc->dup_acks >= 3) {
            
            FastRetransmission(host, ackFrame, curr_timeval);
            cc->ssthresh = (cc->cwnd / 2.0 > 2.0) ? (cc->cwnd / 2.0) : 2.0;
            cc->cwnd = cc->ssthresh + 3.0;
            cc->state = cc_FRFT;
            return;
        }
        
        // On new ACK (not duplicate), increase cwnd by 1
        else if (seq_num_diff(reciever->LAR , ackFrame->seq_num) > 0 && cc->cwnd <= cc->ssthresh) {
            cc->cwnd += 1.0;
            cc->dup_acks = 0;

            // Transition to AIMD if cwnd exceeds ssthresh
            if (cc->cwnd > cc->ssthresh) {
                cc->state = cc_AIMD;
                // Exit to avoid double incrementing
                return;
            }
            return;
        }


    }

    // ----------------------------------------------AIMD State--------------------------------------------------------------

    // AIMD (Additive Increase Multiplicative Decrease) state
    else if (cc->state == cc_AIMD) {

        // increment duplicate acks if recieved
        cc->dup_acks = (seq_num_diff(reciever->LAR, ackFrame->seq_num) <= 0) ? (cc->dup_acks + 1) : 0;

        // If 3 duplicate ACKs, initiate fast retransmission
        if (cc->dup_acks >= 3) {
            FastRetransmission(host, ackFrame, curr_timeval);
            cc->ssthresh = (cc->cwnd / 2.0 > 2.0) ? (cc->cwnd / 2.0) : 2.0;
            cc->cwnd = cc->ssthresh + 3.0;
            cc->state = cc_FRFT;
            return;
        }

        // NEW ACK - Additive increase of cwnd if above ssthresh
        else if (seq_num_diff(reciever->LAR , ackFrame->seq_num) > 0 && cc->cwnd >= cc->ssthresh) {
            cc->cwnd += (1.0/cc->cwnd);
            cc->dup_acks = 0;
            return;
        }


    }


    // ----------------------------------------------FRFT State--------------------------------------------------------------

    // Fast Recovery / Fast Retransmission (FRFT) state
    else if (cc->state == cc_FRFT) {
        
        // On duplicate ACK, increment cwnd
        if (reciever->LAR == ackFrame->seq_num) {
            cc->cwnd = cc->cwnd + 1;
            return;
        }

        // On new ACK - reset dup_acks, set cwnd to ssthresh, and transition to AIMD
        else {
            cc->dup_acks = 0;
            cc->cwnd = cc->ssthresh;
            cc->state = cc_AIMD;
            return;
        }
    }

    // Catch-all for undefined states (error handling)
    else {
        fprintf(stderr, "ERROR - Entered No State \n");
    }
}

struct timeval* host_get_next_expiring_timeval(Host* host) {

    struct timeval* earliestTimeout = NULL;
     for (int i = 0; i < glb_sysconfig.window_size; i++) {
        if (host->send_window[i].frame != NULL && host->send_window[i].timeout != NULL) {
            struct timeval* currentTimeout = host->send_window[i].timeout;
            if (earliestTimeout == NULL || timeval_usecdiff(currentTimeout, earliestTimeout) > 0) {
                earliestTimeout = currentTimeout;
            }
        }
    }
    
    return earliestTimeout;
}

void handle_incoming_acks(Host* host, struct timeval curr_timeval) {

    // Num of acks received from each receiver
    uint8_t num_acks_received[glb_num_hosts]; 
    memset(num_acks_received, 0, glb_num_hosts); 

    // Num of duplicate acks received from each receiver this rtt
    uint8_t num_dup_acks_for_this_rtt[glb_num_hosts];     //PA1b
    memset(num_dup_acks_for_this_rtt, 0, glb_num_hosts); 
    
    uint8_t incomingFrameCRC_Calculation = 0;
    int length = ll_get_length(host->incoming_frames_head);

    while (length > 0) {

        LLnode* currNodeHead = ll_pop_node(&host->incoming_frames_head);
        length = ll_get_length(host->incoming_frames_head);

        if (currNodeHead == NULL) {
            continue;
        }

        Frame* currNodeToFrame = currNodeHead->value;

        if (currNodeToFrame != NULL) {
            char* frameToChar = convert_frame_to_char(currNodeToFrame);
            incomingFrameCRC_Calculation = compute_crc8(frameToChar);
        
            if (incomingFrameCRC_Calculation != 0) {
                ll_destroy_node(currNodeHead);
                continue;
            }

            // ! Enter
            TCPCongestionControl(host, currNodeToFrame, curr_timeval);

            // ! ack sender id - ackframe
            int senderSrcId = currNodeToFrame->src_id;
            RecieverState* reciever = &host->recieverStructure[senderSrcId];
            uint8_t CurrSeq = currNodeToFrame->seq_num;

            num_acks_received[senderSrcId]++;

            if (seq_num_diff(reciever->LAR , CurrSeq) > 0) { 
                reciever->LAR = CurrSeq;

                for (int i = 0; i < glb_sysconfig.window_size; i++) {
                    if (host->send_window[i].frame != NULL && seq_num_diff(host->send_window[i].frame->seq_num, CurrSeq) >= 0 ) {
                        host->send_window[i].frame = NULL;
                        host->send_window[i].timeout = NULL;
                    }
                }
                shiftLeft(host);
            }  
            else if (seq_num_diff(reciever->LAR , CurrSeq) <= 0) {
                num_dup_acks_for_this_rtt[senderSrcId]++;
            }
        }
    }

    if (host->id == glb_sysconfig.host_send_cc_id) {
        fprintf(cc_diagnostics,"%d,%d,%d,",host->round_trip_num, num_acks_received[glb_sysconfig.host_recv_cc_id], num_dup_acks_for_this_rtt[glb_sysconfig.host_recv_cc_id]); 
    }
}

void handle_input_cmds(Host* host, struct timeval curr_timeval) {
    
    int input_cmd_length = ll_get_length(host->input_cmdlist_head);
    
    while (input_cmd_length > 0) {
        LLnode* ll_input_cmd_node = ll_pop_node(&host->input_cmdlist_head);
        input_cmd_length = ll_get_length(host->input_cmdlist_head);
        Cmd* outgoing_cmd = (Cmd*)ll_input_cmd_node->value;
        free(ll_input_cmd_node);

        int msgLength = strlen(outgoing_cmd->message)+1;

        int offset = 0;
        uint16_t bytesRemaining = msgLength;
        int copyUpToBytes = 0;

        while (bytesRemaining > 0) {

            // amount of bytes to copy over
            copyUpToBytes = (bytesRemaining < FRAME_PAYLOAD_SIZE) ? bytesRemaining : FRAME_PAYLOAD_SIZE -1;

            // create outgoing Frame
            Frame* outgoingFrame = malloc(sizeof(Frame));
            assert(outgoingFrame);

            // copy data to frame
            strncpy(outgoingFrame->data, outgoing_cmd->message + offset, copyUpToBytes);
            outgoingFrame->data[copyUpToBytes] = '\0'; 

            // update values
            outgoingFrame->remaining_msg_bytes = bytesRemaining - copyUpToBytes;
            outgoingFrame->src_id = outgoing_cmd->src_id;
            outgoingFrame->dst_id = outgoing_cmd->dst_id;

            // get reciever state
            uint8_t senderDstId = outgoing_cmd->dst_id;
            RecieverState* reciever = &host->recieverStructure[senderDstId];

            // adjust and update values
            outgoingFrame->seq_num = reciever->seqNum;
            reciever->seqNum = reciever->seqNum + 1;
            outgoingFrame->crc_val = 0;

            char* make_frame_char  = convert_frame_to_char(outgoingFrame);
            outgoingFrame->crc_val = compute_crc8(make_frame_char);
            free(make_frame_char);
            
            // append to buffered
            ll_append_node(&host->buffered_outframes_head, outgoingFrame);

            // adjust offset and remaining bytes
            offset += copyUpToBytes;
            bytesRemaining -= copyUpToBytes;
        }
        free(outgoing_cmd->message);
        free(outgoing_cmd);
    }
}

// ***----------------------------------------------------------------------------------------------------------------------***
//                                                 HANDLE TIMEDOUT FRAMES ORIGINAL
// ***----------------------------------------------------------------------------------------------------------------------***

// void handle_timedout_frames(Host* host, struct timeval curr_timeval) {

//     for (int i = 0; i < glb_sysconfig.window_size; i++) {
//         if ( host->send_window[i].frame != NULL && host->send_window[i].timeout != NULL) {
//             struct timeval* ithFrameTimeout = host->send_window[i].timeout;
//             if (timeval_usecdiff(ithFrameTimeout, &curr_timeval) >= 0) {
//                 host->send_window[i].timeout = NULL;
//             }
//         }
//     }
// }

// ***----------------------------------------------------------------------------------------------------------------------***
//                                                           SEPERATION
// ***----------------------------------------------------------------------------------------------------------------------***



// ! modified version
void handle_timedout_frames(Host* host, struct timeval curr_timeval) {

    for (int i = 0; i < glb_sysconfig.window_size; i++) {
        if (host->send_window[i].frame == NULL || host->send_window[i].timeout == NULL) {continue; }

        uint8_t dstId = host->send_window[i].frame->dst_id;
        CongestionControl* cc = &host->cc[dstId];

        if ( host->send_window[i].frame != NULL && host->send_window[i].timeout != NULL) {
            struct timeval* ithFrameTimeout = host->send_window[i].timeout;
            // look for timedout Frames
            if (timeval_usecdiff(ithFrameTimeout, &curr_timeval) >= 0) {
                cc->ssthresh = (cc->cwnd / 2.0 > 2.0) ? (cc->cwnd / 2.0) : 2.0;
                cc->cwnd = 1.0;
                cc->state = cc_SS;

                for (int j = 0; j < glb_sysconfig.window_size; j++) {
                    if ( host->send_window[j].frame != NULL) {
                        host->send_window[j].timeout = NULL;
                    }
                }
                break;
            }
        }
    }
}


// ! modified version
void handle_outgoing_frames(Host* host, struct timeval curr_timeval) {

    long additional_ts = 0; 

    if (timeval_usecdiff(&curr_timeval, host->latest_timeout) > 0) {
        memcpy(&curr_timeval, host->latest_timeout, sizeof(struct timeval)); 
    }

    for (int i = 0; i < glb_sysconfig.window_size; i++) {
        if (host->send_window[i].frame != NULL && host->send_window[i].timeout == NULL) {

            uint8_t dstId = host->send_window[i].frame->dst_id;
            RecieverState* reciever = &host->recieverStructure[dstId];
            CongestionControl* cc = &host->cc[dstId];

            if (reciever->numSent < (int) cc->cwnd) {

                reciever->numSent += 1;
                Frame* outgoingFrame = host->send_window[i].frame;
                Frame* copyOfOutgoingFrame = malloc(sizeof(Frame));
                assert(copyOfOutgoingFrame);
                memcpy(copyOfOutgoingFrame, outgoingFrame, sizeof(Frame));
                ll_append_node(&host->outgoing_frames_head, copyOfOutgoingFrame);
                struct timeval* next_timeout = malloc(sizeof(struct timeval));
                memcpy(next_timeout, &curr_timeval, sizeof(struct timeval)); 
                timeval_usecplus(next_timeout, TIMEOUT_INTERVAL_USEC);
                host->send_window[i].timeout = next_timeout;
            }
        }
    }

    for (int i = 0; i < glb_sysconfig.window_size && ll_get_length(host->buffered_outframes_head) > 0; i++) {
        
        if (host->send_window[i].frame == NULL) {

            Frame* peekedFrame = (Frame*) ll_peek_node(host->buffered_outframes_head);

            uint8_t dstId = peekedFrame->dst_id;
            RecieverState* reciever = &host->recieverStructure[dstId];
            CongestionControl* cc = &host->cc[dstId];

            if (reciever->numSent < (int) cc->cwnd) {

                LLnode* ll_outframe_node = ll_pop_node(&host->buffered_outframes_head);
                Frame* outgoing_frame = ll_outframe_node->value;
                Frame* copyOfOutgoingFrame = malloc(sizeof(Frame));
                assert(copyOfOutgoingFrame);
                memcpy(copyOfOutgoingFrame, outgoing_frame, sizeof(Frame));
                reciever->numSent += 1;
                ll_append_node(&host->outgoing_frames_head, copyOfOutgoingFrame); 
                struct timeval* next_timeout = malloc(sizeof(struct timeval));
                memcpy(next_timeout, &curr_timeval, sizeof(struct timeval)); 
                timeval_usecplus(next_timeout, TIMEOUT_INTERVAL_USEC + additional_ts);
                additional_ts += 10000;
                host->send_window[i].frame = outgoing_frame;
                host->send_window[i].timeout = next_timeout;
                free(ll_outframe_node);
                continue;
            }
        }
    }

    memcpy(host->latest_timeout, &curr_timeval, sizeof(struct timeval)); 
    timeval_usecplus(host->latest_timeout, additional_ts);
    
    //NOTE:
    // Don't worry about latest_timeout field for PA1a, but you need to understand what it does.
    // You may or may not use it in PA1b when you implement fast recovery & fast retransmit in handle_incoming_acks(). 
    // If you choose to retransmit a frame in handle_incoming_acks() in PA1b, all you need to do is:

    // ****************************************
    // long additional_ts = 0; 
    // if (timeval_usecdiff(&curr_timeval, host->latest_timeout) > 0) {
    //     memcpy(&curr_timeval, host->latest_timeout, sizeof(struct timeval)); 
    // }

    //  YOUR FRFT CODE FOES HERE

    // memcpy(host->latest_timeout, &curr_timeval, sizeof(struct timeval)); 
    // timeval_usecplus(host->latest_timeout, additional_ts);
    // ****************************************


    // It essentially fixes the following problem:
    
    // 1) You send out 8 frames from sender0. 
    // Frame 1: curr_time + 0.1 + additional_ts(0.01) 
    // Frame 2: curr_time + 0.1 + additional_ts(0.02) 
    // …

    // 2) Next time you send frames from sender0
    // Curr_time could be less than previous_curr_time + 0.1 + additional_ts. 
    // which means for example frame 9 will potentially timeout faster than frame 6 which shouldn’t happen. 

    // Latest timeout fixes that. 

}


// ***----------------------------------------------------------------------------------------------------------------------***
//                                                 HANDLE OUTGOING FRAMES ORIGINAL
// ***----------------------------------------------------------------------------------------------------------------------***



// void handle_outgoing_frames(Host* host, struct timeval curr_timeval) {

//     long additional_ts = 0; 

//     if (timeval_usecdiff(&curr_timeval, host->latest_timeout) > 0) {
//         memcpy(&curr_timeval, host->latest_timeout, sizeof(struct timeval)); 
//     }

//     // Send out the frames that have timed out(i.e. timeout = NULL)
//     // ! I think this should be floor(cc->cwnd); or ceil if 3.8 for instance
//     for (int i = 0; i < glb_sysconfig.window_size; i++) {
//         if (host->send_window[i].frame != NULL && host->send_window[i].timeout == NULL) {

//             Frame* outgoingFrame = host->send_window[i].frame;
//             Frame* copyOfOutgoingFrame = malloc(sizeof(Frame));
//             assert(copyOfOutgoingFrame);
//             memcpy(copyOfOutgoingFrame, outgoingFrame, sizeof(Frame));
//             ll_append_node(&host->outgoing_frames_head, copyOfOutgoingFrame);
//             struct timeval* next_timeout = malloc(sizeof(struct timeval));
//             memcpy(next_timeout, &curr_timeval, sizeof(struct timeval)); 
//             timeval_usecplus(next_timeout, TIMEOUT_INTERVAL_USEC);
//             host->send_window[i].timeout = next_timeout;
//         }
//     }

//     for (int i = 0; i < glb_sysconfig.window_size && ll_get_length(host->buffered_outframes_head) > 0; i++) {
//         if (host->send_window[i].frame == NULL) {

//             LLnode* ll_outframe_node = ll_pop_node(&host->buffered_outframes_head);
//             Frame* outgoing_frame = ll_outframe_node->value;
//             Frame* copyOfOutgoingFrame = malloc(sizeof(Frame));
//             assert(copyOfOutgoingFrame);
//             memcpy(copyOfOutgoingFrame, outgoing_frame, sizeof(Frame));
//             ll_append_node(&host->outgoing_frames_head, copyOfOutgoingFrame); 
//             struct timeval* next_timeout = malloc(sizeof(struct timeval));
//             memcpy(next_timeout, &curr_timeval, sizeof(struct timeval)); 
//             timeval_usecplus(next_timeout, TIMEOUT_INTERVAL_USEC + additional_ts);
//             additional_ts += 10000;

//             host->send_window[i].frame = outgoing_frame;
//             host->send_window[i].timeout = next_timeout;

//             free(ll_outframe_node);
//         }
//     }

//     memcpy(host->latest_timeout, &curr_timeval, sizeof(struct timeval)); 
//     timeval_usecplus(host->latest_timeout, additional_ts);
    
//     //NOTE:
//     // Don't worry about latest_timeout field for PA1a, but you need to understand what it does.
//     // You may or may not use it in PA1b when you implement fast recovery & fast retransmit in handle_incoming_acks(). 
//     // If you choose to retransmit a frame in handle_incoming_acks() in PA1b, all you need to do is:

//     // ****************************************
//     // long additional_ts = 0; 
//     // if (timeval_usecdiff(&curr_timeval, host->latest_timeout) > 0) {
//     //     memcpy(&curr_timeval, host->latest_timeout, sizeof(struct timeval)); 
//     // }

//     //  YOUR FRFT CODE FOES HERE

//     // memcpy(host->latest_timeout, &curr_timeval, sizeof(struct timeval)); 
//     // timeval_usecplus(host->latest_timeout, additional_ts);
//     // ****************************************


//     // It essentially fixes the following problem:
    
//     // 1) You send out 8 frames from sender0. 
//     // Frame 1: curr_time + 0.1 + additional_ts(0.01) 
//     // Frame 2: curr_time + 0.1 + additional_ts(0.02) 
//     // …

//     // 2) Next time you send frames from sender0
//     // Curr_time could be less than previous_curr_time + 0.1 + additional_ts. 
//     // which means for example frame 9 will potentially timeout faster than frame 6 which shouldn’t happen. 

//     // Latest timeout fixes that. 

// }


// ***----------------------------------------------------------------------------------------------------------------------***
//                                                           DO NOT PASS
// ***----------------------------------------------------------------------------------------------------------------------***



// WE HIGHLY RECOMMEND TO NOT MODIFY THIS FUNCTION
void run_senders() {
    int sender_order[glb_num_hosts]; 
    get_rand_seq(glb_num_hosts, sender_order); 

    for (int i = 0; i < glb_num_hosts; i++) {
        int sender_id = sender_order[i]; 
        struct timeval curr_timeval;

        gettimeofday(&curr_timeval, NULL);

        Host* host = &glb_hosts_array[sender_id]; 

        // Check whether anything has arrived
        int input_cmd_length = ll_get_length(host->input_cmdlist_head);
        int inframe_queue_length = ll_get_length(host->incoming_frames_head);
        struct timeval* next_timeout = host_get_next_expiring_timeval(host); 
        
        // Conditions to "wake up" the host:
        //    1) Acknowledgement or new command
        //    2) Timeout      
        int incoming_frames_cmds = (input_cmd_length != 0) | (inframe_queue_length != 0); 
        long reached_timeout = (next_timeout != NULL) && (timeval_usecdiff(&curr_timeval, next_timeout) <= 0);

        host->awaiting_ack = 0; 
        host->active = 0; 
        host->csv_out = 0; 

        if (incoming_frames_cmds || reached_timeout) {
            host->round_trip_num += 1; 
            host->csv_out = 1; 

            // Implement this
            handle_input_cmds(host, curr_timeval); 
            // Implement this
            handle_incoming_acks(host, curr_timeval);
            // Implement this
            handle_timedout_frames(host, curr_timeval);
            // Implement this
            handle_outgoing_frames(host, curr_timeval); 



            for (int i = 0; i < glb_num_hosts; i++) {
                Host* senderId = &glb_hosts_array[i];
                for (int j = 0; j < glb_num_hosts; j++) {
                    RecieverState* reciever = &senderId->recieverStructure[j];
                    reciever->numSent = 0;
                }
            }

        }


        //Check if we are waiting for acks
        for (int j = 0; j < glb_sysconfig.window_size; j++) {
            if (host->send_window[j].frame != NULL) {
                host->awaiting_ack = 1; 
                break; 
            }
        }

        //Condition to indicate that the host is active 
        if (host->awaiting_ack || ll_get_length(host->buffered_outframes_head) > 0) {
            host->active = 1; 
        }
    }
}