/*
 * Copyright (c) 2016 Jean-Noel Braun.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "bcnn_deconv_layer.h"

#ifdef BCNN_USE_BLAS
#include "cblas.h"
#endif

#include <bh/bh_string.h>

#include "bcnn_activation_layer.h"
#include "bcnn_mat.h"
#include "bcnn_utils.h"

/* Deconv layer */
bcnn_status bcnn_add_deconvolutional_layer(bcnn_net *net, int n, int size,
                                           int stride, int pad,
                                           bcnn_filler_type init,
                                           bcnn_activation activation,
                                           char *src_id, char *dst_id) {
    int i, sz;
    float std_init = 0.0f;
    bcnn_gauss_gen g = {0};
    bcnn_node node = {0};
    bcnn_tensor dst_tensor = {0};

    if (net->num_nodes > 0) {
        int is_src_node_found = 0;
        for (i = net->num_tensors - 1; i >= 0; --i) {
            if (strcmp(net->tensors[i].name, src_id) == 0) {
                bcnn_node_add_input(net, &node, i);
                is_src_node_found = 1;
                break;
            }
        }
        BCNN_CHECK_AND_LOG(
            net->log_ctx, is_src_node_found, BCNN_INVALID_PARAMETER,
            "Deconvolution layer: invalid input node name %s", src_id);
    } else {
        bcnn_node_add_input(net, &node, 0);
    }

    // Create layer
    node.layer = (bcnn_layer *)calloc(1, sizeof(bcnn_layer));
    node.layer->type = DECONVOLUTIONAL;
    node.layer->num = n;
    node.layer->stride = stride;
    node.layer->size = size;
    node.layer->pad = pad;
    node.layer->gemm_ctx = net->gemm_ctx;
    // Create weights tensor
    bcnn_tensor weights = {0};
    char weights_name[256];
    sprintf(weights_name, "%s_w", src_id);
    bcnn_tensor_create(&weights, 1, 1, 1,
                       net->tensors[node.src[0]].c * n * size * size, 1,
                       weights_name);
    bcnn_tensor_filler w_filler = {
        .range = (size * size * net->tensors[node.src[0]].c), .type = init};
    bcnn_tensor_fill(&weights, w_filler);
    bcnn_net_add_tensor(net, weights);
    bcnn_node_add_input(net, &node, net->num_tensors - 1);
    // Create bias tensor
    bcnn_tensor biases = {0};
    char biases_name[256];
    sprintf(biases_name, "%s_b", src_id);
    bcnn_tensor_create(&biases, 1, 1, 1, n, 1, biases_name);
    bcnn_net_add_tensor(net, biases);
    bcnn_node_add_input(net, &node, net->num_tensors - 1);

    if (net->learner.optimizer == ADAM) {
        int weights_size = bcnn_tensor_size(&weights);
        node.layer->adam_m = (float *)calloc(weights_size, sizeof(float));
        node.layer->adam_v = (float *)calloc(weights_size, sizeof(float));
    }

    bcnn_tensor_set_shape(
        &dst_tensor, net->tensors[node.src[0]].n, node.layer->num,
        node.layer->stride * (net->tensors[node.src[0]].h - 1) +
            node.layer->size - 2 * node.layer->pad,
        node.layer->stride * (net->tensors[node.src[0]].w - 1) +
            node.layer->size - 2 * node.layer->pad,
        1);
    bcnn_tensor_allocate(&dst_tensor);
    bh_strfill(&dst_tensor.name, dst_id);
    // Add node to net
    bcnn_net_add_tensor(net, dst_tensor);
    // Add tensor output index to node
    bcnn_node_add_output(net, &node, net->num_tensors - 1);
    sz = net->tensors[node.dst[0]].w * net->tensors[node.dst[0]].h *
         net->tensors[node.src[0]].c * size * size;
    node.layer->conv_workspace = (float *)calloc(sz, sizeof(float));

#ifdef BCNN_USE_CUDA
    sz = net->tensors[node.dst[0]].w * net->tensors[node.dst[0]].h *
         net->tensors[node.src[0]].c * size * size;
    node.layer->conv_workspace_gpu =
        bcnn_cuda_memcpy_f32(node.layer->conv_workspace, sz);
    if (net->learner.optimizer == ADAM) {
        int weights_size = bcnn_tensor_size(&weights);
        node.layer->adam_m_gpu =
            bcnn_cuda_memcpy_f32(node.layer->adam_m, weights_size);
        node.layer->adam_v_gpu =
            bcnn_cuda_memcpy_f32(node.layer->adam_v, weights_size);
    }
#endif
    node.layer->activation = activation;

    bcnn_net_add_node(net, node);

    BCNN_INFO(
        net->log_ctx,
        "[Deconvolutional] input_shape= %dx%dx%d nb_filters= %d kernel_size= "
        "%d stride= %d output_shape= %dx%dx%d\n",
        net->tensors[node.src[0]].w, net->tensors[node.src[0]].h,
        net->tensors[node.src[0]].c, n, size, stride,
        net->tensors[node.dst[0]].w, net->tensors[node.dst[0]].h,
        net->tensors[node.dst[0]].c);

    return BCNN_SUCCESS;
}

int bcnn_forward_deconv_layer_cpu(bcnn_layer *layer, bcnn_tensor *src_tensor,
                                  bcnn_tensor *dst_tensor, bcnn_tensor *weights,
                                  bcnn_tensor *biases) {
    int batch_size = src_tensor->n;
    int i, m, n, k, sz;

    sz = batch_size * dst_tensor->w * dst_tensor->h * dst_tensor->c;

    bcnn_fill_f32(sz, 0.0f, dst_tensor->data);

    m = layer->num * layer->size * layer->size;
    k = src_tensor->c;
    n = src_tensor->w * src_tensor->h;
    sz = src_tensor->c * src_tensor->h * src_tensor->w;
    for (i = 0; i < batch_size; ++i) {
        bcnn_gemm(layer->gemm_ctx, 1, 0, m, n, k, 1.0f, weights->data, m,
                  src_tensor->data + i * sz, n, 0.0f, layer->conv_workspace, n);
        bcnn_col2im(
            layer->conv_workspace, layer->num, dst_tensor->h, dst_tensor->w,
            layer->size, 0, layer->stride,
            dst_tensor->data + i * layer->num * dst_tensor->w * dst_tensor->h);
    }

    bcnn_add_bias(dst_tensor->data, biases->data, batch_size, layer->num,
                  dst_tensor->w * dst_tensor->h);

    sz = dst_tensor->w * dst_tensor->h * dst_tensor->c * batch_size;
    bcnn_forward_activation_cpu(dst_tensor->data, sz, layer->activation);

    return BCNN_SUCCESS;
}

int bcnn_backward_deconv_layer_cpu(bcnn_layer *layer, bcnn_tensor *src_tensor,
                                   bcnn_tensor *dst_tensor,
                                   bcnn_tensor *weights, bcnn_tensor *biases) {
    int batch_size = src_tensor->n;
    int i, sz = src_tensor->w * src_tensor->h * src_tensor->c;
    int m = src_tensor->c;
    int n = layer->size * layer->size * dst_tensor->c;
    int k = src_tensor->w * src_tensor->h;
    float *pdst = NULL;
    float alpha = 1.0f / batch_size;

    bcnn_backward_activation_cpu(
        dst_tensor->data, dst_tensor->grad_data,
        dst_tensor->w * dst_tensor->h * dst_tensor->c * batch_size,
        layer->activation);

    bcnn_grad_bias(biases->grad_data, dst_tensor->grad_data, batch_size,
                   layer->num, dst_tensor->w * dst_tensor->h);

    for (i = 0; i < batch_size; ++i) {
        pdst = dst_tensor->grad_data +
               i * layer->num * dst_tensor->w * dst_tensor->h;
        bcnn_im2col(pdst, dst_tensor->c, dst_tensor->h, dst_tensor->w,
                    layer->size, 0, layer->stride, layer->conv_workspace);
        bcnn_gemm(layer->gemm_ctx, 0, 1, m, n, k, alpha,
                  src_tensor->data +
                      i * src_tensor->c * src_tensor->h * src_tensor->w,
                  k, layer->conv_workspace, k, 1.0f, weights->grad_data, n);

        if (src_tensor->grad_data) {
            bcnn_gemm(layer->gemm_ctx, 0, 0, src_tensor->c, k, n, 1.0f,
                      weights->data, n, layer->conv_workspace, k, 0.0f,
                      src_tensor->grad_data + i * sz, k);
        }
    }
    return BCNN_SUCCESS;
}

#ifdef BCNN_USE_CUDA
int bcnn_forward_deconv_layer_gpu(bcnn_layer *layer, bcnn_tensor *src_tensor,
                                  bcnn_tensor *dst_tensor, bcnn_tensor *weights,
                                  bcnn_tensor *biases) {
    int i, m, n, k, sz;
    int batch_size = dst_tensor->n;

    sz = batch_size * dst_tensor->w * dst_tensor->h * dst_tensor->c;
    bcnn_cuda_fill_f32(sz, 0, dst_tensor->data_gpu, 1);

    m = layer->num * layer->size * layer->size;
    k = src_tensor->c;
    n = src_tensor->w * src_tensor->h;
    sz = src_tensor->c * src_tensor->h * src_tensor->w;
    for (i = 0; i < batch_size; ++i) {
        bcnn_cuda_gemm(1, 0, m, n, k, 1.0f, weights->data_gpu, m,
                       src_tensor->data_gpu + i * sz, n, 0.0f,
                       layer->conv_workspace_gpu, n);
        bcnn_cuda_col2im(layer->conv_workspace_gpu, layer->num, dst_tensor->h,
                         dst_tensor->w, layer->size, layer->stride, 0,
                         dst_tensor->data_gpu +
                             i * layer->num * dst_tensor->w * dst_tensor->h);
    }

    bcnn_cuda_add_bias(dst_tensor->data_gpu, biases->data_gpu, batch_size,
                       layer->num, dst_tensor->w * dst_tensor->h);

    sz = dst_tensor->w * dst_tensor->h * dst_tensor->c * batch_size;
    bcnn_forward_activation_gpu(dst_tensor->data_gpu, sz, layer->activation);

    return BCNN_SUCCESS;
}

int bcnn_backward_deconv_layer_gpu(bcnn_layer *layer, bcnn_tensor *src_tensor,
                                   bcnn_tensor *dst_tensor,
                                   bcnn_tensor *weights, bcnn_tensor *biases) {
    int i, sz = src_tensor->w * src_tensor->h * src_tensor->c;
    int m = src_tensor->c;
    int n = layer->size * layer->size * dst_tensor->c;
    int k = src_tensor->w * src_tensor->h;
    int batch_size = src_tensor->n;
    float *a = NULL, *b = NULL, *c = NULL, *pdst = NULL;
    float alpha = 1.0f / batch_size;

    bcnn_backward_activation_gpu(
        dst_tensor->data_gpu, dst_tensor->grad_data_gpu,
        dst_tensor->w * dst_tensor->h * dst_tensor->c * batch_size,
        layer->activation);

    bcnn_cuda_grad_bias(biases->grad_data_gpu, dst_tensor->grad_data_gpu,
                        batch_size, layer->num, dst_tensor->h * dst_tensor->w);

    for (i = 0; i < batch_size; ++i) {
        a = src_tensor->data_gpu +
            i * src_tensor->c * src_tensor->w * src_tensor->h;
        b = layer->conv_workspace_gpu;
        c = weights->grad_data_gpu;

        pdst = dst_tensor->grad_data_gpu +
               i * dst_tensor->c * dst_tensor->w * dst_tensor->h;

        bcnn_cuda_im2col(pdst, dst_tensor->c, dst_tensor->h, dst_tensor->w,
                         layer->size, layer->stride, 0,
                         layer->conv_workspace_gpu);
        bcnn_cuda_gemm(0, 1, m, n, k, alpha, a, k, b, k, 1.0f, c, n);

        if (src_tensor->grad_data_gpu) {
            a = weights->data_gpu;
            b = layer->conv_workspace_gpu;
            c = src_tensor->grad_data_gpu + i * sz;
            bcnn_cuda_gemm(0, 0, src_tensor->c, k, n, 1.0f, a, n, b, k, 0.0f, c,
                           k);
        }
    }
    return 0;
}
#endif

int bcnn_forward_deconv_layer(bcnn_net *net, bcnn_node *node) {
    bcnn_tensor *src = &net->tensors[node->src[0]];
    bcnn_tensor *dst = &net->tensors[node->dst[0]];
    bcnn_tensor *weights = &net->tensors[node->src[1]];
    bcnn_tensor *biases = &net->tensors[node->src[2]];
#ifdef BCNN_USE_CUDA
    return bcnn_forward_deconv_layer_gpu(node->layer, src, dst, weights,
                                         biases);
#else
    return bcnn_forward_deconv_layer_cpu(node->layer, src, dst, weights,
                                         biases);
#endif
}

int bcnn_backward_deconv_layer(bcnn_net *net, bcnn_node *node) {
    bcnn_tensor *src = &net->tensors[node->src[0]];
    bcnn_tensor *dst = &net->tensors[node->dst[0]];
    bcnn_tensor *weights = &net->tensors[node->src[1]];
    bcnn_tensor *biases = &net->tensors[node->src[2]];
#ifdef BCNN_USE_CUDA
    return bcnn_backward_deconv_layer_gpu(node->layer, src, dst, weights,
                                          biases);
#else
    return bcnn_backward_deconv_layer_cpu(node->layer, src, dst, weights,
                                          biases);
#endif
}