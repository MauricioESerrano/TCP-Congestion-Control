Name: Mauricio Serrano

PID: A17020958

For sender.c

For "host_get_next_expiring_timeval(Host* host)"
This function iterates through the send window of a host to find the earliest timeout value, returning the pointer to the corresponding struct timeval. If no timeouts are present, it returns NULL.

For "handle_timedout_frames(Host* host, struct timeval curr_timeval)"
This function checks the send window for frames that have timed out and resets their timeout values to NULL if they have exceeded the current time. This is used to manage the retransmission of frames that have not been acknowledged in a timely manner.

For "handle_outgoing_frames(Host* host, struct timeval curr_timeval)"
This function manages the transmission of frames from the send window to the outgoing frames list, setting appropriate timeouts for each frame. It ensures that frames are sent only when their timeout conditions allow and updates the latest timeout for the host.

For the most part, functionality for "host_get_next_expiring_timeval(Host* host)", For "handle_timedout_frames(Host* host, struct timeval curr_timeval)", and "handle_outgoing_frames(Host* host, struct timeval curr_timeval)" is quite simple, as the processes that these functions do are short liners.


Now, 

For "handle_incoming_acks(Host* host, struct timeval curr_timeval)"
This function processes incoming acknowledgment frames, updating the expected acknowledgment number and removing acknowledged frames from the send window. It also tracks the number of acknowledgments and duplicate acknowledgments received from each sender.

For "handle_input_cmds(Host* host, struct timeval curr_timeval)"
This function retrieves commands from the host's input command list, processes them, and splits messages that exceed the FRAME_PAYLOAD_SIZE into multiple frames. It also updates the sequence number for each outgoing frame and calculates their CRC values.


For reciever.c

"handle_incoming_frames(Host* host)"
Sends an acknowledgment frames back to the sender, setting the source and destination IDs, sequence number, and calculating the CRC value before appending it to the host's outgoing frames. I did it this way as the code was quite tedious to look at and repetetitve depending on my iteration that I was doing at the time, therefore I created this for easement of coding structure.

"handle_incoming_frames(Host* host)"
Processes incoming frames for a host. Pops frames from the incoming queue, Validates them by checking their CRC, Determines if they fall within the receiver's window, Stores valid frames in a buffer, Sends ACKs for received frames and processes them sequentially. I have yet to mention them, but I initialized and created minqueue and stored its implementation in util.c as i found this to be the easiest form of processing the culamative acks.

Host.c / common.h / util.c

I have not only minqueue created here, but also a recieverstate, and node for the minqueue. Recieverstate is a old implementation in which i initailzed message buffer unique to each host, but now i basically do that with the minqueue.
