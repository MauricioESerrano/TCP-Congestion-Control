#include "host.h"
#include <assert.h>
#include "switch.h"

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

            else {
                int senderSrcId = currNodeToFrame->src_id;
                RecieverState* reciever = &host->recieverStructure[senderSrcId];
                uint8_t CurrSeq = currNodeToFrame->seq_num;
                
                if ( seq_num_diff(reciever->LAR , CurrSeq) > 0) { 
                    reciever->LAR = CurrSeq;
                    num_acks_received[currNodeToFrame->src_id]++;

                    for (int i = 0; i < glb_sysconfig.window_size; i++) {
                        if (host->send_window[i].frame != NULL && seq_num_diff(host->send_window[i].frame->seq_num, CurrSeq) >= 0 ) {
                            host->send_window[i].frame = NULL;
                            host->send_window[i].timeout = NULL;
                        }
                    }
                }
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

        int msg_length = strlen(outgoing_cmd->message)+1;

        int offset = 0;
        uint16_t remaining_bytes = msg_length;
        int copyUpToBytes = 0;

        while (remaining_bytes > 0) {

            if (remaining_bytes < FRAME_PAYLOAD_SIZE) {
                copyUpToBytes = remaining_bytes;

            } else {
                copyUpToBytes = FRAME_PAYLOAD_SIZE -1;
            }

            Frame* outgoing_frame = malloc(sizeof(Frame));
            assert(outgoing_frame);

            strncpy(outgoing_frame->data, outgoing_cmd->message + offset, copyUpToBytes);
            outgoing_frame->data[copyUpToBytes] = '\0'; 

            outgoing_frame->remaining_msg_bytes = remaining_bytes - copyUpToBytes;
            outgoing_frame->src_id = outgoing_cmd->src_id;
            outgoing_frame->dst_id = outgoing_cmd->dst_id;

            uint8_t senderDstId = outgoing_cmd->dst_id;
            RecieverState* reciever = &host->recieverStructure[senderDstId];

            outgoing_frame->seq_num = reciever->seqNum;
            reciever->seqNum = reciever->seqNum + 1;

            outgoing_frame->crc_val = 0;
            char* make_frame_char  = convert_frame_to_char(outgoing_frame);
            outgoing_frame->crc_val = compute_crc8(make_frame_char);
            free(make_frame_char);
            
            ll_append_node(&host->buffered_outframes_head, outgoing_frame);

            offset += copyUpToBytes;
            remaining_bytes -= copyUpToBytes;
        }
        free(outgoing_cmd->message);
        free(outgoing_cmd);
    }
}



void handle_timedout_frames(Host* host, struct timeval curr_timeval) {

    for (int i = 0; i < glb_sysconfig.window_size; i++) {
        if ( host->send_window[i].frame != NULL && host->send_window[i].timeout != NULL) {
            struct timeval* ithFrameTimeout = host->send_window[i].timeout;
            if (timeval_usecdiff(ithFrameTimeout, &curr_timeval) >= 0) {
                host->send_window[i].timeout = NULL;
            }
        }
    }
}


void handle_outgoing_frames(Host* host, struct timeval curr_timeval) {

    long additional_ts = 0; 

    if (timeval_usecdiff(&curr_timeval, host->latest_timeout) > 0) {
        memcpy(&curr_timeval, host->latest_timeout, sizeof(struct timeval)); 
    }

    //TODO: Send out the frames that have timed out(i.e. timeout = NULL)
    for (int i = 0; i < glb_sysconfig.window_size; i++) {
        if (host->send_window[i].frame != NULL && host->send_window[i].timeout == NULL) {

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

    for (int i = 0; i < glb_sysconfig.window_size && ll_get_length(host->buffered_outframes_head) > 0; i++) {
        if (host->send_window[i].frame == NULL) {

            LLnode* ll_outframe_node = ll_pop_node(&host->buffered_outframes_head);
            Frame* outgoing_frame = ll_outframe_node->value;
            Frame* copyOfOutgoingFrame = malloc(sizeof(Frame));
            assert(copyOfOutgoingFrame);
            memcpy(copyOfOutgoingFrame, outgoing_frame, sizeof(Frame));
            ll_append_node(&host->outgoing_frames_head, copyOfOutgoingFrame); 
            struct timeval* next_timeout = malloc(sizeof(struct timeval));
            memcpy(next_timeout, &curr_timeval, sizeof(struct timeval)); 
            timeval_usecplus(next_timeout, TIMEOUT_INTERVAL_USEC + additional_ts);
            additional_ts += 10000;

            host->send_window[i].frame = outgoing_frame;
            host->send_window[i].timeout = next_timeout;

            free(ll_outframe_node);
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