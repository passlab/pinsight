#include "omp.h"
#include "callback.h"
  

void main()
{
    //if (p->left)
    int a = 0;
    int b = 1;
    int c,d;
    #pragma omp task   // p is firstprivate by default
       // traverse(p->left);
       c = a+b;
        //if (p->right)
    #pragma omp task    // p is firstprivate by default
         d = a-b;
        //traverse(p->right);
}
