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
@ stub CoreUIConfigureTestHost
@ stub CoreUIConfigureUserIntegration
@ stdcall CoreUICreate(ptr)
@ stub CoreUICreateAnonymousStream
@ stub CoreUICreateEx
@ stub CoreUIInitializeTestService
@ stub CoreUIOpenExisting
@ stub CoreUIRouteToTestRegistrar
@ stub CoreUIUninitializeTestService
@ stdcall CreateDispatcherQueueController(long long long ptr)
@ stub CreateDispatcherQueueForCurrentThread
@ stdcall -private DllCanUnloadNow()
@ stdcall -private DllGetActivationFactory(ptr ptr)
@ stub DllGetClassObject
@ stub GetDispatcherQueueForCurrentThread
@ stub MsgBlobCreateShared
@ stub MsgBlobCreateStack
@ stub MsgBufferShare
@ stub MsgRelease
@ stub MsgStringCreateShared
@ stub MsgStringCreateStack
@ stub ServiceMain
@ stub SvchostPushServiceGlobals
