#ifndef SLENDYTUBBIES_TRANSFORM_H
#define SLENDYTUBBIES_TRANSFORM_H
/* Camera transform uses VU0 on PS2; the host retains a scalar reference. */
static void transform_vertices(float (*output)[4],const Vertex *vertices,unsigned count,const float *matrix) {
#ifdef _EE
    if(!count)return;
    __asm__ volatile(
        "lqc2 $vf4,0(%3)\n lqc2 $vf5,16(%3)\n lqc2 $vf6,32(%3)\n lqc2 $vf7,48(%3)\n"
        "1:\n lqc2 $vf8,0(%1)\n"
        "vmulaw $ACC,$vf7,$vf0\n vmaddax $ACC,$vf4,$vf8\n vmadday $ACC,$vf5,$vf8\n vmaddz $vf9,$vf6,$vf8\n"
        "vmul.xyz $vf10,$vf9,$vf9\n vaddy.x $vf10,$vf10,$vf10\n vaddz.x $vf10,$vf10,$vf10\n vmulx.w $vf9,$vf0,$vf10\n"
        "sqc2 $vf9,0(%0)\n addiu %0,%0,16\n addiu %1,%1,32\n addiu %2,%2,-1\n bgtz %2,1b\n nop\n"
        : "+&r"(output),"+&r"(vertices),"+&r"(count):"r"(matrix):"memory");
#else
    for(unsigned i=0;i<count;i++) {
        for(int k=0;k<3;k++)output[i][k]=matrix[k]*vertices[i].x+matrix[4+k]*vertices[i].y+matrix[8+k]*vertices[i].z+matrix[12+k];
        output[i][3]=output[i][0]*output[i][0]+output[i][1]*output[i][1]+output[i][2]*output[i][2];
    }
#endif
}
#endif
