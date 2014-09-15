/*
 *  GPUStress
 *  Copyright (C) 2014 Mateusz Szpakowski
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#pragma OPENCL FP_CONTRACT OFF

static inline float4 polyeval4d(float p0, float p1, float p2, float p3, float p4, float4 x)
{
    return mad(x, mad(x, mad(x, mad(x, p4, p3), p2), p1), p0);
}

kernel void gpuStress(uint n, const global float4* restrict input,
            global float4* restrict output, float p0, float p1, float p2, float p3, float p4)
{
    size_t gid = get_global_id(0);
    size_t lid = get_local_id(0);
    size_t gid2 = GROUPSIZE*get_group_id(0) + ((lid+77)%GROUPSIZE);
    size_t gid3 = GROUPSIZE*get_group_id(0) + ((lid+203)%GROUPSIZE);
    
    float4 x1 = input[gid*4];
    float4 x2 = input[gid*4+1];
    float4 x3 = input[gid*4+2];
    float4 x4 = input[gid*4+3];
    float4 x5 = input[gid2*4];
    float4 x6 = input[gid2*4+1];
    float4 x7 = input[gid2*4+2];
    float4 x8 = input[gid2*4+3];
    float4 x9 = input[gid3*4];
    float4 x10 = input[gid3*4+1];
    float4 x11 = input[gid3*4+2];
    float4 x12 = input[gid3*4+3];
    
    for (uint j = 0; j < KITERSNUM; j++)
    {
        x1 = polyeval4d(p0, p1, p2, p3, p4, x1);
        x2 = polyeval4d(p0, p1, p2, p3, p4, x2);
        x3 = polyeval4d(p0, p1, p2, p3, p4, x3);
        x4 = polyeval4d(p0, p1, p2, p3, p4, x4);
        x5 = polyeval4d(p0, p1, p2, p3, p4, x5);
        x6 = polyeval4d(p0, p1, p2, p3, p4, x6);
        x7 = polyeval4d(p0, p1, p2, p3, p4, x7);
        x8 = polyeval4d(p0, p1, p2, p3, p4, x8);
        x9 = polyeval4d(p0, p1, p2, p3, p4, x9);
        x10 = polyeval4d(p0, p1, p2, p3, p4, x10);
        x11 = polyeval4d(p0, p1, p2, p3, p4, x11);
        x12 = polyeval4d(p0, p1, p2, p3, p4, x12);
    }
    
    barrier(CLK_GLOBAL_MEM_FENCE);
    
    output[gid*4] = (x1+x5+x9)*0.3333333333f;
    output[gid*4+1] = (x2+x6+x10)*0.3333333333f;
    output[gid*4+2] = (x3+x7+x11)*0.3333333333f;
    output[gid*4+3] = (x4+x8+x12)*0.3333333333f;
}
