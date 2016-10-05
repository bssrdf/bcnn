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
#include <bh/bh.h>
#include <bh/bh_string.h>
#include <bh/bh_error.h>

/* include bip image processing lib */
#include <bip/bip.h>

#include "bcnn/bcnn.h"


unsigned char *_pack_char(unsigned char *buffer, char val)
{
    buffer[0] = val;
    return buffer + 1;
}

unsigned char *_pack_int(unsigned char *buffer, int val)
{
	buffer[0] = val >> 24;
    buffer[1] = val >> 16;
    buffer[2] = val >> 8;
    buffer[3] = val;
    return buffer + 4;
}


int bcnn_pack_data(char *list, int label_width, bcnn_label_type type, char *out_pack)
{
	FILE *f_lst = NULL, *f_out = NULL;
	char *line = NULL;
	int n = 0, n_tok = 0;
	char **tok = NULL;
	int i, w, h, c, buf_sz, li;
	float lf;
	unsigned char *img = NULL;
	unsigned char *buf = NULL;
	
	f_lst = fopen(list, "rt");
	if (f_lst == NULL) {
		fprintf(stderr, "[ERROR] Can not open %s\n", f_lst);
		return -1;
	}

	while ((line = bh_fgetline(f_lst)) != NULL) {
		n++;
		bh_free(line);
	}
	rewind(f_lst);


	fwrite(&n, 1, sizeof(int), f_out);
	fwrite(&label_width, 1, sizeof(int), f_out);
	fwrite(&type, 1, sizeof(int), f_out);

	switch (type) {
	case LABEL_INT:
		while ((line = bh_fgetline(f_lst)) != NULL) {
			n_tok = bh_strsplit(line, ' ', &tok);
			bh_assert((n_tok - 1 == label_width),
				"Data and label_width are not consistent", BCNN_INVALID_DATA);
			bip_load_image(tok[0], &img, &w, &h, &c);
			bip_write_image_to_memory(&buf, &buf_sz, img, w, h, c, w * c);
			// Write img
			fwrite(&buf_sz, 1, sizeof(buf_sz), f_out);
			fwrite(&buf, 1, buf_sz, f_out);
			// Write label(s)
			for (i = 1; i < n_tok; ++i) {
				li = atoi(tok[i]);
				fwrite(&li, 1, sizeof(li), f_out);
			}
			n++;
			bh_free(line);
			for (i = 0; i < n_tok; ++i) bh_free(tok[i]);
			bh_free(tok);
			bh_free(buf);
			bh_free(img);
		}
		break;
	case LABEL_FLOAT:
		while ((line = bh_fgetline(f_lst)) != NULL) {
			n_tok = bh_strsplit(line, ' ', &tok);
			bh_assert((n_tok - 1 == label_width),
				"Data and label_width are not consistent", BCNN_INVALID_DATA);
			bip_load_image(tok[0], &img, &w, &h, &c);
			bip_write_image_to_memory(&buf, &buf_sz, img, w, h, c, w * c);
			// Write img
			fwrite(&buf_sz, 1, sizeof(buf_sz), f_out);
			fwrite(&buf, 1, buf_sz, f_out);
			bh_free(buf);
			bh_free(img);
			// Write label(s)
			for (i = 1; i < n_tok; ++i) {
				lf = (float)atof(tok[i]);
				fwrite(&lf, 1, sizeof(lf), f_out);
			}
			n++;
			bh_free(line);
			for (i = 0; i < n_tok; ++i) bh_free(tok[i]);
			bh_free(tok);
		}
		break;
	case LABEL_IMG:
		while ((line = bh_fgetline(f_lst)) != NULL) {
			n_tok = bh_strsplit(line, ' ', &tok);
			bh_assert((n_tok == 2),
				"Data and label_width are not consistent", BCNN_INVALID_DATA);
			bip_load_image(tok[0], &img, &w, &h, &c);
			bip_write_image_to_memory(&buf, &buf_sz, img, w, h, c, w * c);
			// Write img
			fwrite(&buf_sz, 1, sizeof(buf_sz), f_out);
			fwrite(&buf, 1, buf_sz, f_out);
			bh_free(buf);
			bh_free(img);
			// Write img as label
			bip_load_image(tok[1], &img, &w, &h, &c);
			bip_write_image_to_memory(&buf, &buf_sz, img, w, h, c, w * c);
			fwrite(&buf_sz, 1, sizeof(buf_sz), f_out);
			fwrite(&buf, 1, buf_sz, f_out);
			bh_free(buf);
			bh_free(img);
			n++;
			bh_free(line);
			for (i = 0; i < n_tok; ++i) bh_free(tok[i]);
			bh_free(tok);		
		}
		break;
	}

	return 0;
}


/* IO */
int bcnn_load_image_from_csv(char *str, int w, int h, int c, unsigned char **img)
{
	int i, n_tok, sz = w * h * c;
	char **tok = NULL;
	uint8_t *ptr_img = NULL;

	n_tok = bh_strsplit(str, ',', &tok);

	bh_assert(n_tok == sz, "Incorrect data size in csv", BCNN_INVALID_DATA);

	ptr_img = (unsigned char *)calloc(sz, sizeof(unsigned char));
	for (i = 0; i < n_tok; ++i) {
		ptr_img[i] = (unsigned char)atoi(tok[i]);
	}
	*img = ptr_img;

	for (i = 0; i < n_tok; ++i)
		bh_free(tok[i]);
	bh_free(tok);

	return BCNN_SUCCESS;
}

/* Mnist iter */
unsigned int _read_int(char *v)
{
	int i;
	unsigned int ret = 0;

	for (i = 0; i < 4; ++i) {
		ret <<= 8;
		ret |= (unsigned char)v[i];
	}

	return ret;
}

int bcnn_mnist_next_iter(bcnn_net *net, bcnn_iterator *iter)
{
	char tmp[16];
	unsigned char l;
	unsigned int n_img = 0, n_labels = 0, width = 0, height = 0;
	size_t n = 0;
	
	if (fread((char *)&l, 1, sizeof(char), iter->f_input) == 0)
		rewind(iter->f_input);
	else
		fseek(iter->f_input, -1, SEEK_CUR);
	if (fread((char *)&l, 1, sizeof(char), iter->f_label) == 0)
		rewind(iter->f_label);
	else
		fseek(iter->f_label, -1, SEEK_CUR);

	if (ftell(iter->f_input) == 0 && ftell(iter->f_label) == 0) {
		fread(tmp, 1, 16, iter->f_input);
		n_img = _read_int(tmp + 4);
		iter->input_height = _read_int(tmp + 8);
		iter->input_width = _read_int(tmp + 12);
		fread(tmp, 1, 8, iter->f_label);
		n_labels = _read_int(tmp + 4);
		bh_assert(n_img == n_labels, "MNIST data: number of images and labels must be the same", 
			BCNN_INVALID_DATA);
		bh_assert(net->input_node.h == iter->input_height && net->input_node.w == iter->input_width,
			"MNIST data: incoherent image width and height",
			BCNN_INVALID_DATA);
		iter->n_samples = n_img;
	}

	// Read label
	n = fread((char *)&l, 1, sizeof(char), iter->f_label);
	iter->label_int[0] = (int)l;
	// Read img
	n = fread(iter->input_uchar, 1, iter->input_width * iter->input_height, iter->f_input);

	return BCNN_SUCCESS;
}

/* Data augmentation */
int bcnn_data_augmentation(uint8_t *img, int width, int height, int depth, bcnn_data_augment *param,
	uint8_t *buffer)
{
	int sz = width * height * depth;
	uint8_t *img_scale = NULL;
	int x_ul = 0, y_ul = 0, w_scale, h_scale;
	float scale = 1.0f, theta = 0.0f, contrast = 1.0f, kx, ky, distortion;
	int brightness = 0;

	if (param->range_shift_x || param->range_shift_y) {
		memset(buffer, 0, sz);
		if (param->use_precomputed) {
			x_ul = param->shift_x;
			y_ul = param->shift_y;
		}
		else {
			x_ul = (int)((float)(rand() - RAND_MAX / 2) / RAND_MAX * param->range_shift_x);
			y_ul = (int)((float)(rand() - RAND_MAX / 2) / RAND_MAX * param->range_shift_y);
			param->shift_x = x_ul;
			param->shift_y = y_ul;
		}
		//bip_crop_image(img, width, height, depth, x_ul, y_ul, buffer, width, height, depth);
		bip_crop_image(img, width, height, width * depth, x_ul, y_ul, buffer, width, height, width * depth, depth);
		memcpy(img, buffer, sz * sizeof(uint8_t));
	}
	if (param->max_scale > 0.0f || param->min_scale > 0.0f) {
		if (param->use_precomputed) {
			scale = param->scale;
		}
		else {
			scale = (((float)rand() / RAND_MAX) * (param->max_scale - param->min_scale) +
				param->min_scale);
			param->scale = scale;
		}
		w_scale = (int)(width * scale); 
		h_scale = (int)(height * scale);
		img_scale = (uint8_t *)calloc(w_scale * h_scale * depth, sizeof(uint8_t));
		bip_resize_bilinear(img, width, height, width * depth, img_scale, w_scale, h_scale, w_scale * depth, depth);
		//bip_crop_image(img_scale, w_scale, h_scale, depth, x_ul, y_ul, img, width, height, depth);
		bip_crop_image(img_scale, w_scale, h_scale, w_scale * depth, x_ul, y_ul, img, width, height, width * depth, depth);
		bh_free(img_scale);
	}
	if (param->rotation_range > 0.0f) {
		if (param->use_precomputed) {
			theta = param->rotation;
		}
		else {
			theta = bip_deg2rad((float)(rand() - RAND_MAX / 2) / RAND_MAX  * param->rotation_range);
			param->rotation = theta;
		}
		memset(buffer, 0, sz);
		bip_rotate_image(img, width, height, width, buffer, width, height, width, depth, theta, width / 2, height / 2, BILINEAR);
		memcpy(img, buffer, width * height * depth * sizeof(uint8_t));
	}
	if (param->min_contrast > 0.0f || param->max_contrast > 0.0f) {
		if (param->use_precomputed) {
			contrast = param->contrast;
		}
		else {
			contrast = (((float)rand() / RAND_MAX) * (param->max_contrast - param->min_contrast) +
				param->min_contrast);
			param->contrast = contrast;
		}
		bip_contrast_stretch(img, width * depth, width, height, depth, img, width * depth, contrast);	
	}
	if (param->min_brightness != 0 || param->max_brightness != 0) {
		if (param->use_precomputed) {
			brightness = param->brightness;
		}
		else {
			brightness = (int)(((float)rand() / RAND_MAX) * (param->max_brightness - param->min_brightness) +
				param->min_brightness);
			param->brightness = brightness;
		}
		bip_image_brightness(img, width * depth, width, height, depth, img, width * depth, brightness);
	}
	if (param->max_distortion > 0.0f) {
		if (param->use_precomputed) {
			kx = param->distortion_kx;
			ky = param->distortion_ky;
			distortion = param->distortion;
		}
		else {
			kx = (((float)rand() - RAND_MAX / 2) / RAND_MAX);
			ky = (((float)rand() - RAND_MAX / 2) / RAND_MAX);
			distortion = ((float)rand() / RAND_MAX) * (param->max_distortion);
			param->distortion_kx = kx;
			param->distortion_ky = ky;
			param->distortion = distortion;
		}
		bip_image_perlin_distortion(img, width * depth, width, height, depth, buffer, width * depth,
			param->distortion, kx, ky);
		memcpy(img, buffer, width * height * depth * sizeof(uint8_t));
	}

	return BCNN_SUCCESS;
}


int bcnn_init_mnist_iterator(bcnn_iterator *iter, char *path_img, char *path_label)
{
	FILE *f_img = NULL, *f_label = NULL;
	char tmp[16] = { 0 };
	int n_img = 0, n_lab = 0;

	iter->type = ITER_MNIST;
	f_img = fopen(path_img, "rb");
	if (f_img == NULL) {
		fprintf(stderr, "[ERROR] Cound not open file %s\n", f_img);
		return -1;
	}
	f_label = fopen(path_label, "rb");
	if (f_label == NULL) {
		fprintf(stderr, "[ERROR] Cound not open file %s\n", f_label);
		return -1;
	}

	iter->f_input = f_img;
	iter->f_label = f_label;
	iter->n_iter = 0;
	// Read header
	fread(tmp, 1, 16, iter->f_input);
	n_img = _read_int(tmp + 4);
	iter->input_height = _read_int(tmp + 8);
	iter->input_width = _read_int(tmp + 12);
	fread(tmp, 1, 8, iter->f_label);
	n_lab = _read_int(tmp + 4);
	bh_assert(n_img == n_lab, "Inconsistent MNIST data: number of images and labels must be the same",
		BCNN_INVALID_DATA);

	iter->input_uchar = (unsigned char *)calloc(iter->input_width * iter->input_height, sizeof(unsigned char));
	iter->label_int = (int *)calloc(1, sizeof(int));
	rewind(iter->f_input);
	rewind(iter->f_label);

	return 0;
}


int bcnn_free_mnist_iterator(bcnn_iterator *iter)
{
	if (iter->f_input != NULL)
		fclose(iter->f_input);
	if (iter->f_label != NULL)
		fclose(iter->f_label);
	bh_free(iter->input_uchar);
	bh_free(iter->label_int);

	return 0;
}


int bcnn_init_list_iterator(bcnn_iterator *iter, char *path_input)
{
	int i;
	FILE *f_list = NULL;
	char *line = NULL;
	char **tok = NULL;
	int n_tok = 0;
	unsigned char *img = NULL;

	iter->type = ITER_LIST;

	f_list = fopen(path_input, "rb");
	if (f_list == NULL) {
		fprintf(stderr, "[ERROR] Can not open file %s\n", f_list);
		return BCNN_INVALID_PARAMETER;
	}

	line = bh_fgetline(f_list);
	n_tok = bh_strsplit(line, ' ', &tok);
	bip_load_image(tok[0], &img, &(iter->input_width), &(iter->input_height), &(iter->input_depth));
	iter->input_uchar = (unsigned char *)calloc(iter->input_width * iter->input_height * iter->input_depth,
		sizeof(unsigned char));

	iter->label_float = (float *)calloc(n_tok - 1, sizeof(float));

	rewind(f_list);
	iter->f_input = f_list;
	bh_free(line);
	bh_free(img);
	for (i = 0; i < n_tok; ++i) bh_free(tok[i]);
	bh_free(tok);
	return BCNN_SUCCESS;
}


int bcnn_free_list_iterator(bcnn_iterator *iter)
{
	if (iter->f_input != NULL)
		fclose(iter->f_input);
	bh_free(iter->input_uchar);
	bh_free(iter->label_float);
	return BCNN_SUCCESS;
}

int bcnn_free_iterator(bcnn_iterator *iter)
{
	if (iter->type == ITER_MNIST)
		return bcnn_free_mnist_iterator(iter);
	else if (iter->type == ITER_LIST)
		return bcnn_free_list_iterator(iter);
	
	return BCNN_SUCCESS;
}

int bcnn_init_iterator(bcnn_iterator *iter, char *path_input, char *path_label, char *type)
{
	
	if (strcmp(type, "mnist") == 0) {
		return bcnn_init_mnist_iterator(iter, path_input, path_label);
	}
	else if (strcmp(type, "bin") == 0) {
	}
	else if (strcmp(type, "list") == 0) {
		return bcnn_init_list_iterator(iter, path_input);
	}
	else if (strcmp(type, "csv") == 0) {
		
	}
	
	return BCNN_SUCCESS;
}