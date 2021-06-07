#pragma once

void put_char(Char c);

void clear_region(int x1, int y1, int x2, int y2);

int cursor_up(int amount);
int cursor_down(int amount);
void cursor_right(int amount);
void cursor_left(int amount);
void cursor_to(int x, int y);
void index(int amount);
void reverse_index(int amount);
void backspace(void);
void save_cursor(void);
void restore_cursor(void);
void back_tab(int n);
void forward_tab(int n);

void delete_chars(int n);
void insert_blank(int n);
void delete_lines(int n);
void insert_lines(int n);
void erase_characters(int n);
