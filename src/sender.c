#include "host.h"
#include <assert.h>
#include "switch.h"

struct timeval* host_get_next_expiring_timeval(Host* host) {

    // TODO: You should fill in this function so that it returns the 
    // timeval when next timeout should occur
    // 
    // 1) Check your send_window for the timeouts of the frames. 
    // 2) Return the timeout of a single frame. 
    // HINT: It's not the frame with the furtherst/latest timeout.

    // ! added \/ --------------------------------------------------------------

    struct timeval* earliestTimeout = NULL;
     for (int i = 0; i < glb_sysconfig.window_size; i++) {
        if (host->send_window[i].timeout != NULL) {
            struct timeval* currentTimeout = host->send_window[i].timeout;

            if (earliestTimeout == NULL || timeval_usecdiff(currentTimeout, earliestTimeout) > 0) {
                earliestTimeout = currentTimeout;
            }
        }
    }
    
    return earliestTimeout;

    // ! -----------------------------------------------------------------------
}

void handle_incoming_acks(Host* host, struct timeval curr_timeval) {

    // Num of acks received from each receiver
    uint8_t num_acks_received[glb_num_hosts]; 
    memset(num_acks_received, 0, glb_num_hosts); 

    // Num of duplicate acks received from each receiver this rtt
    uint8_t num_dup_acks_for_this_rtt[glb_num_hosts];     //PA1b
    memset(num_dup_acks_for_this_rtt, 0, glb_num_hosts); 

    // TODO: Suggested steps for handling incoming ACKs
    //    1) Dequeue the ACK frame from host->incoming_frames_head
    //    2) Compute CRC of the ack frame to know whether it is corrupted
    //    3) Check if the ack is valid i.e. within the window slot 
    //    4) Implement logic as per sliding window protocol to track ACK for what frame is expected,
    //       and what to do when ACK for expected frame is received

    // ! added \/ --------------------------------------------------------------
    
    uint8_t incomingFrameCRC_Calculation = 0;
    int expectAck = host->awaiting_ack;
    int length = ll_get_length(host->incoming_frames_head);

    // printf("Debug Handle Incoming Ack 1 \n");
    while (length > 0) {
        // printf("Debug Handle Incoming Ack 2 \n");
        LLnode* currNodeHead = ll_pop_node(&host->incoming_frames_head);
        length = ll_get_length(host->incoming_frames_head);

        if (currNodeHead == NULL) {
            break;
        }

        Frame* currNodeToFrame = currNodeHead->value;

        if (currNodeToFrame != NULL) {
            char* frameToChar = convert_frame_to_char(currNodeToFrame);
            incomingFrameCRC_Calculation = compute_crc8(frameToChar);
        
            if (incomingFrameCRC_Calculation != 0) {
                printf("data is corrupted for ACK %d\n", currNodeToFrame->seq_num);
                ll_destroy_node(currNodeHead);
                continue;
            }
            else {

                // printf("-------------------------------------------------------- \n");

                uint8_t CurrAckVal = currNodeToFrame->seq_num;
                // printf("SENDER -curr Ack Value = %d \n", CurrAckVal);
                int wrapAroundValue = seq_num_diff(expectAck, CurrAckVal);

                // recieved expected ack
                // wraparoundvalue
                if (wrapAroundValue >= 0 && wrapAroundValue < glb_sysconfig.window_size) {
                    expectAck = (CurrAckVal + 1) % 256;
                    host->awaiting_ack = expectAck;
                    printf("SENDER - ACK %d received, advancing window to expect ACK %d\n", CurrAckVal, expectAck);
                    num_acks_received[currNodeToFrame->src_id]++;

                    ll_destroy_node(currNodeHead);

                    for (int i = 0; i < glb_sysconfig.window_size; i++) {
                        // printf("SENDER - 1\n");
                        if (host->send_window[i].frame != NULL && CurrAckVal == host->send_window[i].frame->seq_num) {
                            // printf("SENDER - 2\n");
                            free(host->send_window[i].frame);
                            free(host->send_window[i].timeout);
                            // do i need to do this part? \/
                            host->send_window[i].frame = NULL;
                            host->send_window[i].timeout = NULL;
                        }
                    }
                }
            }
        }

        // free(currNodeHead);
        // free(currNodeToFrame);

        // printf("Debug Handle Incoming Ack 5 \n");
    }


    // ! -----------------------------------------------------------------------

    if (host->id == glb_sysconfig.host_send_cc_id) {
        fprintf(cc_diagnostics,"%d,%d,%d,",host->round_trip_num, num_acks_received[glb_sysconfig.host_recv_cc_id], num_dup_acks_for_this_rtt[glb_sysconfig.host_recv_cc_id]); 
    }
}

void handle_input_cmds(Host* host, struct timeval curr_timeval) {


    int input_cmd_length = ll_get_length(host->input_cmdlist_head);

    while (input_cmd_length > 0) {

        LLnode* ll_input_cmd_node = ll_pop_node(&host->input_cmdlist_head);
        input_cmd_length = ll_get_length(host->input_cmdlist_head);


        Cmd* outgoing_cmd = (Cmd*) ll_input_cmd_node->value;
        free(ll_input_cmd_node);

        int msg_length = strlen(outgoing_cmd->message) + 1;
        int bytes_remaining = msg_length;
        int seq_num = 0;
        

        while (bytes_remaining > 0) {
            
            int ByteToCopy;
            if (bytes_remaining > FRAME_PAYLOAD_SIZE) {
                ByteToCopy = FRAME_PAYLOAD_SIZE;
            }
            else {
                ByteToCopy = bytes_remaining;
            }

            Frame* outgoing_frame = malloc(sizeof(Frame));
            assert(outgoing_frame);
            outgoing_frame->src_id = outgoing_cmd->src_id;
            outgoing_frame->dst_id = outgoing_cmd->dst_id;
            seq_num += 1;
            outgoing_frame->seq_num = seq_num;

            memcpy(outgoing_frame->data, outgoing_cmd->message + (msg_length - bytes_remaining), ByteToCopy);

            outgoing_frame->remaining_msg_bytes = bytes_remaining - ByteToCopy;
            bytes_remaining -= ByteToCopy;

            outgoing_frame->crc_val = 0;
            char* outgoingFrameToChar = convert_frame_to_char(outgoing_frame);
            outgoing_frame->crc_val = compute_crc8(outgoingFrameToChar);

            ll_append_node(&host->buffered_outframes_head, outgoing_frame);

            free(outgoingFrameToChar);

        } 

        free(outgoing_cmd->message);
        free(outgoing_cmd);
    }
}


void handle_timedout_frames(Host* host, struct timeval curr_timeval) {

    // TODO: Detect frames that have timed out
    // Check your send_window for the frames that have timed out and set send_window[i]->timeout = NULL
    // You will re-send the actual frames and set the timeout in handle_outgoing_frames()
    for (int i = 0; i < glb_sysconfig.window_size; i++) {
        
        if (host->send_window[i].timeout != NULL) {
            struct timeval* ithFrameTimeout = host->send_window[i].timeout;
            if (timeval_usecdiff(ithFrameTimeout, &curr_timeval) <= 0) {
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
        if (host->send_window[i].timeout == NULL && host->send_window[i].frame != NULL) {
            
            Frame* outgoingFrame = host->send_window[i].frame;
            Frame* copyOfOutgoingFrame = malloc(sizeof(Frame));
            assert(copyOfOutgoingFrame);
            memcpy(copyOfOutgoingFrame, outgoingFrame, sizeof(Frame));
            // copy to outgoing frame change back
            ll_append_node(&host->outgoing_frames_head, copyOfOutgoingFrame);

            struct timeval* next_timeout = malloc(sizeof(struct timeval));
            memcpy(next_timeout, &curr_timeval, sizeof(struct timeval)); 
            timeval_usecplus(next_timeout, TIMEOUT_INTERVAL_USEC + additional_ts);
            additional_ts += 10000; //ADD ADDITIONAL 10ms

            host->send_window[i].timeout = next_timeout;
        }
    }

    //TODO: The code is incomplete and needs to be changed to have a correct behavior
    //Suggested steps: 
    //1) Within the for loop, check if the window is not full and there's space to send more frames 
    //2) If there is, pop from the buffered_outframes_head queue and fill your send_window_slot data structure with appropriate fields. 
    //3) Append the popped frame to the host->outgoing_frames_head
    for (int i = 0; i < glb_sysconfig.window_size && ll_get_length(host->buffered_outframes_head) > 0; i++) {
        if (host->send_window[i].frame == NULL) {
            LLnode* ll_outframe_node = ll_pop_node(&host->buffered_outframes_head);
            Frame* outgoing_frame = ll_outframe_node->value;
            Frame* copyOfOutgoingFrame = malloc(sizeof(Frame));
            assert(copyOfOutgoingFrame);
            memcpy(copyOfOutgoingFrame, outgoing_frame, sizeof(Frame));
            // copy to outgoing frame change back
            ll_append_node(&host->outgoing_frames_head, copyOfOutgoingFrame); 
            

            //Set a timeout for this frame
            //NOTE: Each dataframe(not ack frame) that is appended to the 
            //host->outgoing_frames_head has to have a 10ms offset from 
            //the previous frame to enable selective retransmission mechanism. 
            //Already implemented below

            struct timeval* next_timeout = malloc(sizeof(struct timeval));
            memcpy(next_timeout, &curr_timeval, sizeof(struct timeval)); 
            timeval_usecplus(next_timeout, TIMEOUT_INTERVAL_USEC + additional_ts);
            additional_ts += 10000; //ADD ADDITIONAL 10ms

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