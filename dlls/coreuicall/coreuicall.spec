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
@ cdecl abs(long) "wine_abs"
@ cdecl labs(long) "wine_labs"
@ cdecl -ret64 llabs(int64) "wine_llabs"
@ cdecl -ret64 imaxabs(int64) "wine_imaxabs"
@ cdecl rand() "wine_rand"
@ cdecl rand_s(ptr) "wine_rand_s"
@ cdecl srand(long) "wine_srand"
@ cdecl bsearch(ptr ptr long long ptr) ucrtbase.bsearch
@ cdecl bsearch_s(ptr ptr long long ptr ptr) ucrtbase.bsearch_s
@ cdecl -ret64 div(long long) ucrtbase.div
@ cdecl -ret64 ldiv(long long) ucrtbase.ldiv
@ cdecl -norelay lldiv(int64 int64) ucrtbase.lldiv
@ cdecl -norelay imaxdiv(int64 int64) ucrtbase.imaxdiv
