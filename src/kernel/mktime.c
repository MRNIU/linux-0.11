#include<time.h>  // 时间头文件，定义了标准时间数据结构 tm 和一些处理时间函数原型
/*
 * This isn't the library routine,it is only used in the kernel.
 * as such,we don't care about years< 1970 etc,but assume everything is ok.
 * Similarly,TZ etc is happily ignore.We just do everything as easily as possible.
 * Let's find something pubilc for the library routines(although I think minix
 * times is pubilc).
 */
/* 这不是库函数，它仅供内核使用。因此我们不关心小于 1970 年的年份等，但假定一切均很正常。
 * 同样，时间区域 TZ 问题也先忽略。我们之事尽可能简单地处理问题。最好能找到一些公开的函数库
 * (尽管我认为 MIINIX 的时间函数是公开的)。
 * 另外，我恨那个设置 1970 年开始的人 - 难道他们就不能选择从一个闰年开始？我恨格里高历、
 * 罗马教皇、主教，我什么都不在乎。我是个脾气暴躁的人。
 */
#define MINUTE 60 // 1 分钟的秒数
#define HOUR (60*MINUTE) // 1 小时的秒数
#define DAY (24*HOUR) // 1 天的秒数
#define YEAR (365*DAY) // 1 年的秒数
// 有趣的是我们考虑进了闰年
// interestingly,we assume leap-years
static int month[12]={  // 下面以年为界限，定义了每个月开始时的秒数时间数组
  0,
	DAY*(31),
	DAY*(31+29),
	DAY*(31+29+31),
	DAY*(31+29+31+30),
	DAY*(31+29+31+30+31),
	DAY*(31+29+31+30+31+30),
	DAY*(31+29+31+30+31+30+31),
	DAY*(31+29+31+30+31+30+31+31),
	DAY*(31+29+31+30+31+30+31+31+30),
	DAY*(31+29+31+30+31+30+31+31+30+31),
	DAY*(31+29+31+30+31+30+31+31+30+31+30)
};
// 该函数计算从 1970 年 1 月 1 日 0 时起到开机当日经过的秒数，作为开机时间
long kernel_mktime(struct tm * tm){
  long res;
  int year;

  year=tm->tm_year-70;  // 从 1970 年到现在经过的年数(2 位表示)，会有千年虫问题
// magic offest (y+1) needed to get leapyears right.
// 为了获得正确的闰年数，这里需要一个魔幻值(y+1).
  res=YEAR*year+DAY*((year+1)/4); // 这些年经过的秒数时间+每个闰年时多 1 天的秒数时间，
  res+=month[tm->tm_mon]; // 再加上当年到当月时的秒数。
// and (y+2) here.If it wasn't a leap-yaar,we have to adjust
// 以及 (y+2)。如果 (y+2) 不是闰年，那么我们就必须进行调整(减去一天的秒数时间)。
  if(tm->tm_mon>1&&((year+2)%4))
    res-=DAY;
  res+=DAY*(tm->tm_mday-1); // 再加上本月过去的天数的秒数时间
  res+=HOUR*tm->tm_hour;  // 再加上当天过去的小时数的秒数时间
  res+=MINUTE*tm->tm_min; // 再加上 1 小时内过去的分钟数的秒数时间
  res+=tm->tm_sec;  // 再加上 1 分钟内已过去的秒数
  return res; // 即等于从 1970 年以来经过的秒数时间
// 闰年计算方法：如果 y 能被 4 整除且不能被 100 整除，或者能被 400 整除，则 y 是闰年。
}
