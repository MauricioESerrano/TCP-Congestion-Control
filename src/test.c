void handle_input_cmds(Host* host, struct timeval curr_timeval) {
    // TODO: Suggested steps for handling input cmd
    //    1) Dequeue the Cmd from host->input_cmdlist_head
    //    2) Implement fragmentation if the message length is larger than FRAME_PAYLOAD_SIZE
    //    3) Set up the frame according to the protocol
    //    4) Append each frame to host->buffered_outframes_head

    int input_cmd_length = ll_get_length(host->input_cmdlist_head);

    while (input_cmd_length > 0) {
        // Pop a node off and update the input_cmd_length
        LLnode* ll_input_cmd_node = ll_pop_node(&host->input_cmdlist_head);
        input_cmd_length = ll_get_length(host->input_cmdlist_head);

        // Cast to Cmd type and free up the memory for the node
        Cmd* outgoing_cmd = (Cmd*) ll_input_cmd_node->value;
        free(ll_input_cmd_node);
 
        int msg_length = strlen(outgoing_cmd->message) + 1; // +1 to account for null terminator 
        if (msg_length > FRAME_PAYLOAD_SIZE) {
            // Do something about messages that exceed the frame size
            printf(
                "<SEND_%d>: sending messages of length greater than %d is not "
                "implemented\n",
                host->id, MAX_FRAME_SIZE);
        } else {
            Frame* outgoing_frame = malloc(sizeof(Frame));
            assert(outgoing_frame);
            strcpy(outgoing_frame->data, outgoing_cmd->message);
            outgoing_frame->src_id = outgoing_cmd->src_id;
            outgoing_frame->dst_id = outgoing_cmd->dst_id;
            // At this point, we don't need the outgoing_cmd
            free(outgoing_cmd->message);
            free(outgoing_cmd);

            ll_append_node(&host->buffered_outframes_head, outgoing_frame);
        }
    }
}