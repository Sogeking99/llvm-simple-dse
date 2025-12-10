#include <stdio.h>

int main(){

    int a = 5; // store
    int b;

    for(int i = 0; i<3; i++){
        b = a;  // load
        a = i +10;  // store
    }

    printf("%d", b);

    return 0;
}