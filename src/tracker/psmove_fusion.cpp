
 /**
 * PS Move API - An interface for the PS Move Motion Controller
 * Copyright (c) 2012 Thomas Perl <m@thp.io>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *    1. Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *
 *    2. Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 **/


#include "psmove_fusion.h"
#include "../psmove_private.h"

#include <stdlib.h>
#include <math.h>
#include <vector>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>

#define PSMOVE_FUSION_STEP_EPSILON (.0001)
#define TRANSFORM_FILE "transform.csv"

struct _PSMoveFusion {
    PSMoveTracker *tracker;

    float width;
    float height;

    glm::mat4 projection;
    glm::mat4 modelview;
    glm::vec4 viewport;

    glm::mat4 physical_xf;
    glm::mat4 total_xf;
};

void
psmove_fusion_load_physical_xf(PSMoveFusion *fusion)
{
    char *transform_file = psmove_util_get_file_path(TRANSFORM_FILE);
    std::ifstream infile;
    infile.open(transform_file);
    if (infile.is_open())
    {
        std::vector<float> row;
        std::string line;
        getline(infile, line);
        std::stringstream ss(line);
        std::string field;
        while (getline(ss, field, ','))
        {
            std::stringstream fs(field);
            float f = 0.0;
            fs >> f;
            row.push_back(f);
        }

        int i;
        for (i = 0; i < 12; i++)
        {
            int row_ix = i % 3;
            int col_ix = (float(i) / 3.0);
            fusion->physical_xf[col_ix][row_ix] = row[i];
        }
        infile.close();
    }
    free(transform_file);
}

void
print_mat4(glm::mat4 in)
{
    printf("glm::mat4\n");
    printf("%f, %f, %f, %f\n%f, %f, %f, %f\n%f, %f, %f, %f\n%f, %f, %f, %f\n",
        in[0][0], in[1][0], in[2][0], in[3][0],
        in[0][1], in[1][1], in[2][1], in[3][1],
        in[0][2], in[1][2], in[2][2], in[3][2],
        in[0][3], in[1][3], in[2][3], in[3][3]);
}

void
psmove_fusion_update_transform(PSMoveFusion *fusion, float *pos_xyz, float *quat_wxyz)
{
    glm::vec3 position(pos_xyz[0], pos_xyz[1], pos_xyz[2]);
    glm::quat quaternion(quat_wxyz[0], quat_wxyz[1], quat_wxyz[2], quat_wxyz[3]);
    glm::mat4 quatmat = glm::mat4_cast(quaternion);
    glm::mat4 posmat = glm::translate(glm::mat4(), position);
    fusion->total_xf = posmat * quatmat * fusion->physical_xf;
    /*
    printf("fusion->total_xf = posmat * quatmat * fusion->physical_xf;\n");
    printf("posmat:\n");
    print_mat4(posmat);
    printf("quatmat:\n");
    print_mat4(quatmat);
    printf("posmat*quatmat:\n");
    print_mat4(posmat*quatmat);
    printf("fusion->physical_xf:\n");
    print_mat4(fusion->physical_xf);
    printf("fusion->total_xf:\n");
    print_mat4(fusion->total_xf);
    */
}

void
psmove_fusion_reset_transform(PSMoveFusion *fusion)
{
    fusion->physical_xf = glm::mat4(1.0f);   // Identity matrix.
    fusion->total_xf = fusion->physical_xf;  // For now there is no additional transform.
}

PSMoveFusion *
psmove_fusion_new(PSMoveTracker *tracker, float z_near, float z_far)
{
    PSMoveFusion *fusion = (PSMoveFusion*)calloc(1, sizeof(PSMoveFusion));

    fusion->tracker = tracker;

    int width, height;
    psmove_tracker_get_size(tracker, &width, &height);

    fusion->width = (float)width;
    fusion->height = (float)height;

    fusion->projection = glm::perspectiveFov<float>(PSEYE_FOV_BLUE_DOT,
            fusion->width, fusion->height, z_near, z_far);
    fusion->viewport = glm::vec4(0., 0., fusion->width, fusion->height);

    fusion->physical_xf = glm::mat4(1.0f);   // Identity matrix.
    psmove_fusion_load_physical_xf(fusion);  // Load transform from file if it exists.
    fusion->total_xf = fusion->physical_xf;  // For now there is no additional transform.

    return fusion;
}

float *
psmove_fusion_get_projection_matrix(PSMoveFusion *fusion)
{
    psmove_return_val_if_fail(fusion != NULL, NULL);

    return glm::value_ptr(fusion->projection);
}

float *
psmove_fusion_get_modelview_matrix(PSMoveFusion *fusion, PSMove *move)
{
    psmove_return_val_if_fail(fusion != NULL, NULL);
    psmove_return_val_if_fail(move != NULL, NULL);

	// Get the orientation of the controller
	glm::quat q;
    psmove_get_orientation(move, &q.w, &q.x, &q.y, &q.z);
	glm::mat4 rotation= glm::mat4_cast(q);
    
	// Get the position of the controller
	glm::vec3 t;
    psmove_fusion_get_position(fusion, move, &t.x, &t.y, &t.z);
	glm::mat4 translation= glm::translate(glm::mat4(), t);

	// When the tracker is has mirroring turned OFF, our image will appear flipped
	// There for we should flip the rotation about the 
	glm::vec3 s= (!psmove_tracker_get_mirror(fusion->tracker)) ? glm::vec3(-1, 1, 1) : glm::vec3(1, 1, 1);
	glm::mat4 scale = glm::scale(glm::mat4(), s);

	// Combine the transforms in reverse order we want them applied
	// 1) Rotate the controller to match the orientation obtained by sensor fusion
	// 2) Flip about the x-axis if video mirroring is turned OFF
	// 3) Translate the controller to match the position given by tracking fusion
    fusion->modelview = translation * scale * rotation;

    return glm::value_ptr(fusion->modelview);
}

float *
psmove_fusion_get_transform_matrix(PSMoveFusion *fusion)
{
    psmove_return_val_if_fail(fusion != NULL, NULL);
    return glm::value_ptr(fusion->total_xf);
}

void
psmove_fusion_get_position(PSMoveFusion *fusion, PSMove *move,
float *x, float *y, float *z)
{
    psmove_return_if_fail(fusion != NULL);
    psmove_return_if_fail(move != NULL);

    float camX, camY, camR;
    psmove_tracker_get_position(fusion->tracker, move, &camX, &camY, &camR);

    float winX = (float)camX;
    float winY = fusion->height - (float)camY;
    float winZ = .5; /* start value for binary search */

    float targetWidth = 2.*camR;

    glm::vec3 obj;

    /* Binary search for the best distance based on the current projection */
    float step = .25;
    while (step > PSMOVE_FUSION_STEP_EPSILON) {
        /* Calculate center position of sphere */
        obj = glm::unProject(glm::vec3(winX, winY, winZ),
            glm::mat4(), fusion->projection, fusion->viewport);

        /* Project left edge center of sphere */
        glm::vec3 left = glm::project(glm::vec3(obj.x - .5, obj.y, obj.z),
            glm::mat4(), fusion->projection, fusion->viewport);

        /* Project right edge center of sphere */
        glm::vec3 right = glm::project(glm::vec3(obj.x + .5, obj.y, obj.z),
            glm::mat4(), fusion->projection, fusion->viewport);

        float width = (right.x - left.x);
        if (width > targetWidth) {
            /* Too near */
            winZ += step;
        }
        else if (width < targetWidth) {
            /* Too far away */
            winZ -= step;
        }
        else {
            /* Perfect fit */
            break;
        }
        step *= .5;
    }

    if (x != NULL) {
        *x = obj.x;
    }

    if (y != NULL) {
        *y = obj.y;
    }

    if (z != NULL) {
        *z = obj.z;
    }
}

void
psmove_fusion_get_location(PSMoveFusion *fusion, PSMove *move,
        float *x, float *y, float *z)
{
    psmove_return_if_fail(fusion != NULL);
    psmove_return_if_fail(move != NULL);

    // Assuming the update_tracker_cbb was called.
    psmove_tracker_get_location(fusion->tracker, move, x, y, z);
}

void
psmove_fusion_get_transformed_location(PSMoveFusion *fusion, PSMove *move,
float *x, float *y, float *z)
{
    float xcm, ycm, zcm;
    psmove_fusion_get_location(fusion, move, &xcm, &ycm, &zcm);

    glm::vec4 location = glm::vec4(glm::vec3(xcm, ycm, zcm), 1.0f);
    glm::vec4 transformed = fusion->total_xf * location;

    *x = transformed[0];
    *y = transformed[1];
    *z = transformed[2];
}

void
psmove_fusion_free(PSMoveFusion *fusion)
{
    free(fusion);
}

