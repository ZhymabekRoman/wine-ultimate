@ stdcall CoreUICallComputeMaximumMessageSize(ptr ptr long long long long ptr)
@ stdcall CoreUICallCreateConversationHost(ptr ptr ptr ptr)
@ stdcall CoreUICallCreateEndpointHost(ptr ptr ptr)
@ stdcall CoreUICallCreateEndpointHostWithSendPriority(ptr long ptr ptr)
@ stdcall CoreUICallGetAddressOfParameterInBuffer(ptr long long ptr)
@ stdcall CoreUICallReceive(ptr ptr ptr long ptr ptr long)
@ varargs CoreUICallSend(ptr ptr long long long ptr)
@ stdcall CoreUICallSendVaList(ptr ptr long long long ptr ptr)
@ stdcall CoreMsgCreateSession(long ptr)
@ stdcall CoreMsgGetExistingSession(ptr)
@ stub CoreUICreateAnonymousStream
@ cdecl qsort(ptr ptr ptr) "wine_qsort"
@ cdecl qsort_s(ptr ptr long ptr ptr) "wine_qsort_s"
@ cdecl rand() "wine_rand"
@ cdecl rand_s(ptr) "wine_rand_s"
@ cdecl srand(long) "wine_srand"
