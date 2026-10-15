//app.h 应用整合层：初始化顺序 + 主循环调度顺序集中在这里
//main.c里只需要两行：App_Init() 放在USER CODE BEGIN 2，App_Poll() 放在while(1)里
#ifndef __APP_H
#define __APP_H

//初始化，顺序不能乱，见app.c里的注释
void App_Init(void);

//主循环每圈调用一次
void App_Poll(void);

#endif
