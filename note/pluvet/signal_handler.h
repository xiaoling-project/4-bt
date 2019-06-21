//	signal_handler.h
#ifndef SIGNAL_handler_H
#define SIGNAL_handler_H

// ��һЩ��������,���ͷŶ�̬������ڴ�
void do_clear_work();

// ����һЩ�ź�
void process_signal(int signo);

// �����źŴ�������
int set_signal_handler();

#endif