"""Standard 2048 line and direction rules extracted from the production game."""
from pathlib import Path
import subprocess,re
ROOT=Path(__file__).resolve().parents[1]
def function(text,name):
 m=re.search(r'static bool '+name+r'\([^;{}]*\)\s*\{',text);assert m;end,depth=m.end(),1
 while depth:depth+=(text[end]=='{')-(text[end]=='}');end+=1
 return text[m.start():end]
def main():
 out=ROOT/'.tools/game-hub-20261115/host-2048';out.mkdir(parents=True,exist_ok=True);source=(ROOT/'src/ui/custom/custom.c').read_text(encoding='utf-8')
 unit=out/'test.c';unit.write_text('#include <assert.h>\n#include <string.h>\n#include <stdbool.h>\n#include <stdio.h>\nstatic int game_grid[4][4],game_score;\n'+function(source,'game_move_line')+'\n'+function(source,'game_move')+r'''
 static void line(const int in[4],const int want[4],int score,bool moved){int x[4];memcpy(x,in,sizeof(x));game_score=0;assert(game_move_line(x)==moved);assert(!memcmp(x,want,sizeof(x))&&game_score==score);}
 int main(){line((int[]){2,2,4,0},(int[]){4,4,0,0},4,true);line((int[]){4,4,8,8},(int[]){8,16,0,0},24,true);line((int[]){2,2,2,2},(int[]){4,4,0,0},8,true);line((int[]){0,2,0,2},(int[]){4,0,0,0},4,true);line((int[]){2,4,8,16},(int[]){2,4,8,16},0,false);line((int[]){0,0,0,0},(int[]){0,0,0,0},0,false);
 int initial[4][4]={{2,0,2,4},{2,2,4,4},{0,8,8,0},{4,0,0,4}},left[4][4]={{4,4,0,0},{4,8,0,0},{16,0,0,0},{8,0,0,0}},right[4][4]={{0,0,4,4},{0,0,4,8},{0,0,0,16},{0,0,0,8}};
 memcpy(game_grid,initial,sizeof(initial));game_score=0;assert(game_move(0,-1)&&!memcmp(game_grid,left,sizeof(left))&&game_score==40);
 memcpy(game_grid,initial,sizeof(initial));game_score=0;assert(game_move(0,1)&&!memcmp(game_grid,right,sizeof(right))&&game_score==40);
 memset(game_grid,0,sizeof(game_grid));game_grid[0][0]=2;game_grid[1][0]=2;game_grid[2][0]=4;game_score=0;assert(game_move(-1,0)&&game_grid[0][0]==4&&game_grid[1][0]==4&&game_grid[2][0]==0&&game_score==4);
 game_score=0;assert(game_move(1,0)&&game_grid[3][0]==8&&game_grid[2][0]==0&&game_score==8);
 puts("2048 rules PASS: each tile merges once per move, score/no-op and all four directions");return 0;}
 ''',encoding='utf-8');exe=out/'rules.exe';subprocess.run([str(ROOT/'.tools/host-tcc/tcc/tcc.exe'),str(unit),'-o',str(exe)],check=True);subprocess.run([str(exe)],check=True)
if __name__=='__main__':main()
