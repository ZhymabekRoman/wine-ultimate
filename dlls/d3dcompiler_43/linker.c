/*
 * Copyright 2025 Zhiyi Zhang for CodeWeavers
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */
#define COBJMACROS
#include "wine/debug.h"
#include "d3dcompiler_private.h"

WINE_DEFAULT_DEBUG_CHANNEL(d3dcompiler);

struct d3d11_linking_node
{
    ID3D11LinkingNode ID3D11LinkingNode_iface;
    LONG refcount;
};

static inline struct d3d11_linking_node *impl_from_ID3D11LinkingNode(ID3D11LinkingNode *iface)
{
    return CONTAINING_RECORD(iface, struct d3d11_linking_node, ID3D11LinkingNode_iface);
}

static HRESULT WINAPI d3d11_linking_node_QueryInterface(ID3D11LinkingNode *iface, REFIID riid, void **object)
{
    if (IsEqualGUID(riid, &IID_ID3D11LinkingNode) || IsEqualGUID(riid, &IID_IUnknown))
    {
        IUnknown_AddRef(iface);
        *object = iface;
        return S_OK;
    }
    *object = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI d3d11_linking_node_AddRef(ID3D11LinkingNode *iface)
{
    struct d3d11_linking_node *node = impl_from_ID3D11LinkingNode(iface);
    return InterlockedIncrement(&node->refcount);
}

static ULONG WINAPI d3d11_linking_node_Release(ID3D11LinkingNode *iface)
{
    struct d3d11_linking_node *node = impl_from_ID3D11LinkingNode(iface);
    ULONG ref = InterlockedDecrement(&node->refcount);
    if (!ref) free(node);
    return ref;
}

static const struct ID3D11LinkingNodeVtbl d3d11_linking_node_vtbl =
{
    d3d11_linking_node_QueryInterface,
    d3d11_linking_node_AddRef,
    d3d11_linking_node_Release,
};

static HRESULT create_linking_node(ID3D11LinkingNode **out)
{
    struct d3d11_linking_node *node;
    if (!out) return E_INVALIDARG;
    if (!(node = calloc(1, sizeof(*node)))) return E_OUTOFMEMORY;
    node->ID3D11LinkingNode_iface.lpVtbl = &d3d11_linking_node_vtbl;
    node->refcount = 1;
    *out = &node->ID3D11LinkingNode_iface;
    return S_OK;
}

struct d3d11_graph_param
{
    char semantic_name[32];
    UINT semantic_index;
    UINT rows;
    UINT cols;
};

struct d3d11_graph_info
{
    UINT input_count;
    struct d3d11_graph_param inputs[16];
    BOOL has_sample_tex0;
    BOOL has_sample_tex1;
    BOOL ignore_alpha0;
    BOOL ignore_alpha1;
    BOOL has_clip_planes;
    char combiner[64];
};

struct d3d11_module_instance
{
    ID3D11ModuleInstance ID3D11ModuleInstance_iface;
    LONG refcount;
    struct d3d11_graph_info info;
    BOOL has_info;
};

static inline struct d3d11_module_instance *impl_from_ID3D11ModuleInstance(ID3D11ModuleInstance *iface)
{
    return CONTAINING_RECORD(iface, struct d3d11_module_instance, ID3D11ModuleInstance_iface);
}

static HRESULT WINAPI d3d11_module_instance_QueryInterface(ID3D11ModuleInstance *iface, REFIID riid, void **object)
{
    if (IsEqualGUID(riid, &IID_ID3D11ModuleInstance) || IsEqualGUID(riid, &IID_IUnknown))
    {
        IUnknown_AddRef(iface);
        *object = iface;
        return S_OK;
    }
    *object = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI d3d11_module_instance_AddRef(ID3D11ModuleInstance *iface)
{
    struct d3d11_module_instance *inst = impl_from_ID3D11ModuleInstance(iface);
    return InterlockedIncrement(&inst->refcount);
}

static ULONG WINAPI d3d11_module_instance_Release(ID3D11ModuleInstance *iface)
{
    struct d3d11_module_instance *inst = impl_from_ID3D11ModuleInstance(iface);
    ULONG ref = InterlockedDecrement(&inst->refcount);
    if (!ref) free(inst);
    return ref;
}

static HRESULT WINAPI d3d11_module_instance_BindConstantBuffer(ID3D11ModuleInstance *iface,
        UINT src_slot, UINT dst_slot, UINT dst_offset)
{
    return S_OK;
}

static HRESULT WINAPI d3d11_module_instance_BindConstantBufferByName(ID3D11ModuleInstance *iface,
        const char *name, UINT dst_slot, UINT dst_offset)
{
    return S_OK;
}

static HRESULT WINAPI d3d11_module_instance_BindResource(ID3D11ModuleInstance *iface,
        UINT src_slot, UINT dst_slot, UINT count)
{
    return S_OK;
}

static HRESULT WINAPI d3d11_module_instance_BindResourceByName(ID3D11ModuleInstance *iface,
        const char *name, UINT dst_slot, UINT count)
{
    return S_OK;
}

static HRESULT WINAPI d3d11_module_instance_BindSampler(ID3D11ModuleInstance *iface,
        UINT src_slot, UINT dst_slot, UINT count)
{
    return S_OK;
}

static HRESULT WINAPI d3d11_module_instance_BindSamplerByName(ID3D11ModuleInstance *iface,
        const char *name, UINT dst_slot, UINT count)
{
    return S_OK;
}

static HRESULT WINAPI d3d11_module_instance_BindUnorderedAccessView(ID3D11ModuleInstance *iface,
        UINT src_slot, UINT dst_slot, UINT count)
{
    return S_OK;
}

static HRESULT WINAPI d3d11_module_instance_BindUnorderedAccessViewByName(ID3D11ModuleInstance *iface,
        const char *name, UINT dst_slot, UINT count)
{
    return S_OK;
}

static HRESULT WINAPI d3d11_module_instance_BindResourceAsUnorderedAccessView(ID3D11ModuleInstance *iface,
        UINT src_slot, UINT dst_slot, UINT count)
{
    return S_OK;
}

static HRESULT WINAPI d3d11_module_instance_BindResourceAsUnorderedAccessViewByName(ID3D11ModuleInstance *iface,
        const char *name, UINT dst_slot, UINT count)
{
    return S_OK;
}

static const struct ID3D11ModuleInstanceVtbl d3d11_module_instance_vtbl =
{
    d3d11_module_instance_QueryInterface,
    d3d11_module_instance_AddRef,
    d3d11_module_instance_Release,
    d3d11_module_instance_BindConstantBuffer,
    d3d11_module_instance_BindConstantBufferByName,
    d3d11_module_instance_BindResource,
    d3d11_module_instance_BindResourceByName,
    d3d11_module_instance_BindSampler,
    d3d11_module_instance_BindSamplerByName,
    d3d11_module_instance_BindUnorderedAccessView,
    d3d11_module_instance_BindUnorderedAccessViewByName,
    d3d11_module_instance_BindResourceAsUnorderedAccessView,
    d3d11_module_instance_BindResourceAsUnorderedAccessViewByName,
};

static HRESULT create_module_instance(ID3D11ModuleInstance **out)
{
    struct d3d11_module_instance *inst;
    if (!out) return E_INVALIDARG;
    if (!(inst = calloc(1, sizeof(*inst)))) return E_OUTOFMEMORY;
    inst->ID3D11ModuleInstance_iface.lpVtbl = &d3d11_module_instance_vtbl;
    inst->refcount = 1;
    *out = &inst->ID3D11ModuleInstance_iface;
    return S_OK;
}

struct d3d11_module
{
    ID3D11Module ID3D11Module_iface;
    LONG refcount;
};

static inline struct d3d11_module *impl_from_ID3D11Module(ID3D11Module *iface)
{
    return CONTAINING_RECORD(iface, struct d3d11_module, ID3D11Module_iface);
}

static HRESULT WINAPI d3d11_module_QueryInterface(ID3D11Module *iface, REFIID riid, void **object)
{
    if (IsEqualGUID(riid, &IID_ID3D11Module) || IsEqualGUID(riid, &IID_IUnknown))
    {
        IUnknown_AddRef(iface);
        *object = iface;
        return S_OK;
    }
    *object = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI d3d11_module_AddRef(ID3D11Module *iface)
{
    struct d3d11_module *mod = impl_from_ID3D11Module(iface);
    return InterlockedIncrement(&mod->refcount);
}

static ULONG WINAPI d3d11_module_Release(ID3D11Module *iface)
{
    struct d3d11_module *mod = impl_from_ID3D11Module(iface);
    ULONG ref = InterlockedDecrement(&mod->refcount);
    if (!ref) free(mod);
    return ref;
}

static HRESULT WINAPI d3d11_module_CreateInstance(ID3D11Module *iface,
        const char *ns, ID3D11ModuleInstance **instance)
{
    TRACE("iface %p, ns %s, instance %p.\n", iface, wine_dbgstr_a(ns), instance);
    return create_module_instance(instance);
}

static const struct ID3D11ModuleVtbl d3d11_module_vtbl =
{
    d3d11_module_QueryInterface,
    d3d11_module_AddRef,
    d3d11_module_Release,
    d3d11_module_CreateInstance,
};

HRESULT d3d11_create_module_stub(ID3D11Module **module)
{
    struct d3d11_module *mod;
    if (!module) return E_INVALIDARG;
    if (!(mod = calloc(1, sizeof(*mod)))) return E_OUTOFMEMORY;
    mod->ID3D11Module_iface.lpVtbl = &d3d11_module_vtbl;
    mod->refcount = 1;
    *module = &mod->ID3D11Module_iface;
    return S_OK;
}

struct d3d11_function_linking_graph
{
    ID3D11FunctionLinkingGraph ID3D11FunctionLinkingGraph_iface;
    LONG refcount;
    struct d3d11_graph_info info;
};

static inline struct d3d11_function_linking_graph *impl_from_ID3D11FunctionLinkingGraph(ID3D11FunctionLinkingGraph *iface)
{
    return CONTAINING_RECORD(iface, struct d3d11_function_linking_graph, ID3D11FunctionLinkingGraph_iface);
}

static HRESULT WINAPI d3d11_function_linking_graph_QueryInterface(ID3D11FunctionLinkingGraph *iface, REFIID riid, void **object)
{
    struct d3d11_function_linking_graph *graph = impl_from_ID3D11FunctionLinkingGraph(iface);

    TRACE("graph %p, riid %s, object %p.\n", graph, debugstr_guid(riid), object);

    if (IsEqualGUID(riid, &IID_ID3D11FunctionLinkingGraph)
            || IsEqualGUID(riid, &IID_IUnknown))
    {
        IUnknown_AddRef(iface);
        *object = iface;
        return S_OK;
    }

    WARN("%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid(riid));
    *object = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI d3d11_function_linking_graph_AddRef(ID3D11FunctionLinkingGraph *iface)
{
    struct d3d11_function_linking_graph *graph = impl_from_ID3D11FunctionLinkingGraph(iface);
    ULONG refcount = InterlockedIncrement(&graph->refcount);

    TRACE("%p increasing refcount to %lu.\n", iface, refcount);

    return refcount;
}

static ULONG WINAPI d3d11_function_linking_graph_Release(ID3D11FunctionLinkingGraph *iface)
{
    struct d3d11_function_linking_graph *graph = impl_from_ID3D11FunctionLinkingGraph(iface);
    ULONG refcount = InterlockedDecrement(&graph->refcount);

    TRACE("%p decreasing refcount to %lu.\n", iface, refcount);

    if (!refcount)
        free(graph);

    return refcount;
}

static HRESULT WINAPI d3d11_function_linking_graph_CreateModuleInstance(ID3D11FunctionLinkingGraph *iface,
        ID3D11ModuleInstance **instance, ID3DBlob **error)
{
    struct d3d11_function_linking_graph *graph = impl_from_ID3D11FunctionLinkingGraph(iface);
    HRESULT hr;

    TRACE("iface %p, instance %p, error %p.\n", iface, instance, error);
    if (error) *error = NULL;
    hr = create_module_instance(instance);
    if (SUCCEEDED(hr) && instance && *instance)
    {
        struct d3d11_module_instance *inst = impl_from_ID3D11ModuleInstance(*instance);
        inst->info = graph->info;
        inst->has_info = TRUE;
    }
    return hr;
}

static HRESULT WINAPI d3d11_function_linking_graph_SetInputSignature(ID3D11FunctionLinkingGraph *iface,
        const D3D11_PARAMETER_DESC *parameter_desc, UINT parameter_count, ID3D11LinkingNode **input_node)
{
    struct d3d11_function_linking_graph *graph = impl_from_ID3D11FunctionLinkingGraph(iface);
    UINT i, n = parameter_count < ARRAY_SIZE(graph->info.inputs) ? parameter_count : ARRAY_SIZE(graph->info.inputs);

    TRACE("iface %p, parameter_desc %p, parameter_count %u, input_node %p.\n", iface,
            parameter_desc, parameter_count, input_node);

    memset(&graph->info, 0, sizeof(graph->info));
    graph->info.input_count = n;
    for (i = 0; i < n && parameter_desc; i++)
    {
        const char *sem = parameter_desc[i].SemanticName ? parameter_desc[i].SemanticName : "TEXCOORD0";
        lstrcpynA(graph->info.inputs[i].semantic_name, sem, sizeof(graph->info.inputs[i].semantic_name));
        graph->info.inputs[i].semantic_index = 0;
        graph->info.inputs[i].rows = parameter_desc[i].Rows ? parameter_desc[i].Rows : 1;
        graph->info.inputs[i].cols = parameter_desc[i].Columns ? parameter_desc[i].Columns : 4;
        TRACE("  input[%u]: %s (%ux%u)\n", i, sem,
              graph->info.inputs[i].rows, graph->info.inputs[i].cols);
    }
    return create_linking_node(input_node);
}

static HRESULT WINAPI d3d11_function_linking_graph_SetOutputSignature(ID3D11FunctionLinkingGraph *iface,
        const D3D11_PARAMETER_DESC *parameter_desc, UINT parameter_count, ID3D11LinkingNode **output_node)
{
    TRACE("iface %p, parameter_desc %p, parameter_count %u, output_node %p.\n", iface,
            parameter_desc, parameter_count, output_node);
    return create_linking_node(output_node);
}

static HRESULT WINAPI d3d11_function_linking_graph_CallFunction(ID3D11FunctionLinkingGraph *iface,
        LPCSTR namespace, ID3D11Module *module, LPCSTR function_name, ID3D11LinkingNode **call_node)
{
    struct d3d11_function_linking_graph *graph = impl_from_ID3D11FunctionLinkingGraph(iface);

    TRACE("iface %p, namespace %s, module %p, function_name %s, call_node %p.\n", iface,
            wine_dbgstr_a(namespace), module, wine_dbgstr_a(function_name), call_node);

    if (function_name)
    {
        if (strstr(function_name, "SampleTexture0")) graph->info.has_sample_tex0 = TRUE;
        else if (strstr(function_name, "SampleTexture1")) graph->info.has_sample_tex1 = TRUE;
        else if (strstr(function_name, "IgnoreAlpha"))
        {
            if (graph->info.has_sample_tex1)
                graph->info.ignore_alpha1 = TRUE;
            else
                graph->info.ignore_alpha0 = TRUE;
        }
        else if (strstr(function_name, "ApplyPSClipPlanes"))
            graph->info.has_clip_planes = TRUE;
        else if (strstr(function_name, "AAPrimitive"))
            lstrcpynA(graph->info.combiner, function_name, sizeof(graph->info.combiner));
    }
    return create_linking_node(call_node);
}

static HRESULT WINAPI d3d11_function_linking_graph_PassValue(ID3D11FunctionLinkingGraph *iface,
        ID3D11LinkingNode *src_node, INT src_parameter_index, ID3D11LinkingNode *dst_node,
        INT dst_parameter_index)
{
    TRACE("iface %p, src_node %p, src_parameter_index %d, dst_node %p, dst_parameter_index %d.\n",
            iface, src_node, src_parameter_index, dst_node, dst_parameter_index);
    return S_OK;
}

static HRESULT WINAPI d3d11_function_linking_graph_PassValueWithSwizzle(ID3D11FunctionLinkingGraph *iface,
        ID3D11LinkingNode *src_node, INT src_parameter_index, LPCSTR src_swizzle,
        ID3D11LinkingNode *dst_node, INT dst_parameter_index, LPCSTR dst_swizzle)
{
    TRACE("iface %p, src_node %p, src_parameter_index %d, src_swizzle %s, dst_node %p, "
          "dst_parameter_index %d, dst_swizzle %s.\n", iface, src_node, src_parameter_index,
          wine_dbgstr_a(src_swizzle), dst_node, dst_parameter_index, wine_dbgstr_a(dst_swizzle));
    return S_OK;
}

static HRESULT WINAPI d3d11_function_linking_graph_GetLastError(ID3D11FunctionLinkingGraph *iface,
        ID3DBlob **error)
{
    FIXME("iface %p, error %p stub!\n", iface, error);
    return E_NOTIMPL;
}

static HRESULT WINAPI d3d11_function_linking_graph_GenerateHlsl(ID3D11FunctionLinkingGraph *iface,
        UINT flags, ID3DBlob **buffer)
{
    FIXME("iface %p, flags %#x, buffer %p stub!\n", iface, flags, buffer);
    return E_NOTIMPL;
}

static const struct ID3D11FunctionLinkingGraphVtbl d3d11_function_linking_graph_vtbl =
{
    d3d11_function_linking_graph_QueryInterface,
    d3d11_function_linking_graph_AddRef,
    d3d11_function_linking_graph_Release,
    d3d11_function_linking_graph_CreateModuleInstance,
    d3d11_function_linking_graph_SetInputSignature,
    d3d11_function_linking_graph_SetOutputSignature,
    d3d11_function_linking_graph_CallFunction,
    d3d11_function_linking_graph_PassValue,
    d3d11_function_linking_graph_PassValueWithSwizzle,
    d3d11_function_linking_graph_GetLastError,
    d3d11_function_linking_graph_GenerateHlsl,
};

HRESULT WINAPI D3DCreateFunctionLinkingGraph(UINT flags, ID3D11FunctionLinkingGraph **graph)
{
    struct d3d11_function_linking_graph *object;

    TRACE("flags %#x, graph %p.\n", flags, graph);

    if (flags != 0 || !graph)
        return E_INVALIDARG;

    if (!(object = calloc(1, sizeof(*object))))
        return E_OUTOFMEMORY;

    object->ID3D11FunctionLinkingGraph_iface.lpVtbl = &d3d11_function_linking_graph_vtbl;
    object->refcount = 1;

    *graph = &object->ID3D11FunctionLinkingGraph_iface;
    return S_OK;
}

struct d3d11_linker
{
    ID3D11Linker ID3D11Linker_iface;
    LONG refcount;
};

static inline struct d3d11_linker *impl_from_ID3D11Linker(ID3D11Linker *iface)
{
    return CONTAINING_RECORD(iface, struct d3d11_linker, ID3D11Linker_iface);
}

static HRESULT WINAPI d3d11_linker_QueryInterface(ID3D11Linker *iface, REFIID riid, void **object)
{
    struct d3d11_linker *linker = impl_from_ID3D11Linker(iface);

    TRACE("linker %p, riid %s, object %p.\n", linker, debugstr_guid(riid), object);

    if (IsEqualGUID(riid, &IID_ID3D11Linker)
            || IsEqualGUID(riid, &IID_IUnknown))
    {
        IUnknown_AddRef(iface);
        *object = iface;
        return S_OK;
    }

    WARN("%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid(riid));
    *object = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI d3d11_linker_AddRef(ID3D11Linker *iface)
{
    struct d3d11_linker *linker = impl_from_ID3D11Linker(iface);
    ULONG refcount = InterlockedIncrement(&linker->refcount);

    TRACE("%p increasing refcount to %lu.\n", iface, refcount);

    return refcount;
}

static ULONG WINAPI d3d11_linker_Release(ID3D11Linker *iface)
{
    struct d3d11_linker *linker = impl_from_ID3D11Linker(iface);
    ULONG refcount = InterlockedDecrement(&linker->refcount);

    TRACE("%p decreasing refcount to %lu.\n", iface, refcount);

    if (!refcount)
        free(linker);

    return refcount;
}

static HRESULT WINAPI d3d11_linker_Link(ID3D11Linker *iface, ID3D11ModuleInstance *instance,
        LPCSTR instance_name, LPCSTR target_name, UINT flags, ID3DBlob **shader, ID3DBlob **error)
{
    static const char vs_src[] =
        "struct VSOut { float4 pos : SV_Position; float2 uv : TEXCOORD0; };\n"
        "VSOut main(float4 pos : POSITION, float2 uv : TEXCOORD0)\n"
        "{\n"
        "    VSOut o; o.pos = pos; o.uv = uv; return o;\n"
        "}\n";
    char hlsl[2048];
    const char *profile = target_name ? target_name : "ps_4_0";
    struct d3d11_module_instance *inst = instance ? impl_from_ID3D11ModuleInstance(instance) : NULL;

    TRACE("iface %p, instance %p, instance_name %s, target_name %s, flags %#x, shader %p, error %p.\n",
            iface, instance, wine_dbgstr_a(instance_name), wine_dbgstr_a(target_name), flags, shader, error);

    if (target_name && target_name[0] == 'v')
        return D3DCompile(vs_src, strlen(vs_src), "linker_vs.hlsl", NULL, NULL, "main", profile, 0, 0, shader, error);

    if (inst && inst->has_info && inst->info.input_count > 0)
    {
        int len = 0;
        UINT i;
        int color_idx = -1, uv0_idx = -1, uv1_idx = -1;
        int clip0_idx = -1, clip1_idx = -1, clip2_idx = -1, clip3_idx = -1;

        if (inst->info.has_sample_tex0)
            len += sprintf(hlsl + len, "Texture2D tex0 : register(t0);\nSamplerState smp0 : register(s0);\n");
        if (inst->info.has_sample_tex1)
            len += sprintf(hlsl + len, "Texture2D tex1 : register(t1);\nSamplerState smp1 : register(s1);\n");

        len += sprintf(hlsl + len, "struct PSIn {\n");
        for (i = 0; i < inst->info.input_count; i++)
        {
            const struct d3d11_graph_param *p = &inst->info.inputs[i];
            UINT c = (p->cols >= 1 && p->cols <= 4) ? p->cols : 4;
            len += sprintf(hlsl + len, "    float%u p%u : %s;\n", c, i, p->semantic_name);
            if (color_idx < 0 && !strnicmp(p->semantic_name, "COLOR", 5))
                color_idx = (int)i;
            if (!stricmp(p->semantic_name, "TEXCOORD0"))
                uv0_idx = (int)i;
            else if (!stricmp(p->semantic_name, "TEXCOORD1"))
                uv1_idx = (int)i;
            else if (uv0_idx < 0 && !strnicmp(p->semantic_name, "TEXCOORD", 8))
                uv0_idx = (int)i;
            else if (!stricmp(p->semantic_name, "CLIPPLANES0"))
                clip0_idx = (int)i;
            else if (!stricmp(p->semantic_name, "CLIPPLANES1"))
                clip1_idx = (int)i;
            else if (!stricmp(p->semantic_name, "CLIPPLANES2"))
                clip2_idx = (int)i;
            else if (!stricmp(p->semantic_name, "CLIPPLANES3"))
                clip3_idx = (int)i;
        }
        len += sprintf(hlsl + len, "};\nfloat4 main(PSIn i) : SV_Target0 {\n");
        if (inst->info.has_clip_planes && clip0_idx >= 0 && clip1_idx >= 0 && clip2_idx >= 0 && clip3_idx >= 0)
        {
            len += sprintf(hlsl + len,
                "    float4 _cd = i.p%d / i.p%d.x + i.p%d;\n"
                "    clip(sign(_cd) - i.p%d);\n",
                clip0_idx, clip3_idx, clip1_idx, clip2_idx);
        }
        if (inst->info.has_sample_tex0 && uv0_idx >= 0)
        {
            len += sprintf(hlsl + len, "    float4 c = tex0.Sample(smp0, i.p%d.xy);\n", uv0_idx);
            if (inst->info.ignore_alpha0)
                len += sprintf(hlsl + len, "    c.a = 1.0;\n");
            if (color_idx >= 0)
                len += sprintf(hlsl + len, "    c *= i.p%d;\n", color_idx);
            if (inst->info.has_sample_tex1 && uv1_idx >= 0)
            {
                len += sprintf(hlsl + len, "    float4 m = tex1.Sample(smp1, i.p%d.xy);\n", uv1_idx);
                if (inst->info.ignore_alpha1)
                    len += sprintf(hlsl + len, "    m.a = 1.0;\n");
                len += sprintf(hlsl + len, "    c *= m.a;\n");
            }
            len += sprintf(hlsl + len, "    return c;\n}\n");
        }
        else if (inst->info.has_sample_tex1 && uv1_idx >= 0)
        {
            len += sprintf(hlsl + len, "    float4 m = tex1.Sample(smp1, i.p%d.xy);\n", uv1_idx);
            if (color_idx >= 0)
                len += sprintf(hlsl + len, "    return i.p%d * m.a;\n}\n", color_idx);
            else
                len += sprintf(hlsl + len, "    return m;\n}\n");
        }
        else if (color_idx >= 0)
        {
            len += sprintf(hlsl + len, "    return i.p%d;\n}\n", color_idx);
        }
        else if (uv0_idx >= 0 && inst->info.inputs[uv0_idx].cols == 4)
        {
            len += sprintf(hlsl + len, "    return i.p%d;\n}\n", uv0_idx);
        }
        else
        {
            len += sprintf(hlsl + len, "    return float4(0.95, 0.95, 0.95, 1.0);\n}\n");
        }
        TRACE("Generated HLSL:\n%s\n", hlsl);
        return D3DCompile(hlsl, len, "linker_ps.hlsl", NULL, NULL, "main", profile, 0, 0, shader, error);
    }

    {
        static const char fallback_ps[] =
            "float4 main(float4 pos : SV_Position, float4 col : COLOR0) : SV_Target0 { return col; }\n";
        return D3DCompile(fallback_ps, strlen(fallback_ps), "linker_ps.hlsl", NULL, NULL, "main", profile, 0, 0, shader, error);
    }
}

static HRESULT WINAPI d3d11_linker_UseLibrary(ID3D11Linker *iface, ID3D11ModuleInstance *instance)
{
    TRACE("iface %p, instance %p.\n", iface, instance);
    return S_OK;
}

static HRESULT WINAPI d3d11_linker_AddClipPlaneFromCBuffer(ID3D11Linker *iface, UINT buffer_slot,
        UINT buffer_entry)
{
    TRACE("iface %p, buffer_slot %u, buffer_entry %u.\n", iface, buffer_slot, buffer_entry);
    return S_OK;
}

static const struct ID3D11LinkerVtbl d3d11_linker_vtbl =
{
    d3d11_linker_QueryInterface,
    d3d11_linker_AddRef,
    d3d11_linker_Release,
    d3d11_linker_Link,
    d3d11_linker_UseLibrary,
    d3d11_linker_AddClipPlaneFromCBuffer,
};

HRESULT WINAPI D3DCreateLinker(ID3D11Linker **linker)
{
    struct d3d11_linker *object;

    TRACE("linker %p.\n", linker);

    if (!linker)
        return E_INVALIDARG;

    if (!(object = calloc(1, sizeof(*object))))
        return E_OUTOFMEMORY;

    object->ID3D11Linker_iface.lpVtbl = &d3d11_linker_vtbl;
    object->refcount = 1;

    *linker = &object->ID3D11Linker_iface;
    return S_OK;
}
