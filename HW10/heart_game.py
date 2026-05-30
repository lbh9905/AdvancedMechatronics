import pgzrun
import pygame
import random
import math
import serial

# ── Pico serial connection ────────────────────────────────────────────────────
pico      = None
left_btn  = False
right_btn = False
try:
    pico = serial.Serial('COM4', timeout=0.01)
    print("Pico connected!")
except:
    print("No Pico found — keyboard only")

# ── Window ────────────────────────────────────────────────────────────────────
WIDTH  = 800
HEIGHT = 600

HOT_PINK    = (255,  20, 147)
PINK        = (255, 105, 180)
DEEP_PINK   = (199,  21, 133)
LIGHT_PINK  = (255, 182, 213)
PURPLE      = (160,   0, 220)
DARK_PURPLE = ( 80,   0, 150)
ORANGE      = (255, 140,   0)
YELLOW      = (255, 220,  50)
WHITE       = (255, 255, 255)
DARK_TEXT   = ( 70,   0,  90)
TEAL        = ( 80, 210, 210)

SUNSET_STRIPS = [
    (195,145,200),(215,140,175),(232,148,152),
    (245,163,125),(252,183,105),(255,204, 90),(255,222,145),
]
GEM_COLORS        = [(25,0,50),(40,0,70),(15,0,35),(50,0,80),(30,0,55)]
FLOWER_PETAL_COLS = [PINK, LIGHT_PINK, (200,100,255), ORANGE, (255,180,80)]

FLOWER_SCORE    = 50
CRYSTAL_PENALTY = 25

def level_cfg(lvl):
    n = lvl - 1
    return {
        'crystal_rate': max(18, 52 - n*5),
        'speed_min':    2.5 + n*0.30,
        'speed_max':    5.2 + n*0.40,
        'heart_rate':   max(130, 210 - n*15),
        'flower_rate':  max(110, 190 - n*12),
        'dist_need':    500 + n*200,
    }

player_x      = float(WIDTH  // 2)
player_y      = float(HEIGHT - 100)
PLAYER_SPEED  = 5
PLAYER_RADIUS = 22

game_state       = 'start'
level            = 1
life             = 100
MAX_LIFE         = 100
score            = 0
high_score       = 0
distance         = 0.0
invincible_timer = 0
anim_timer       = 0
crystal_timer    = 0
heart_timer      = 0
flower_timer     = 0

mom_x            = float(WIDTH // 2)
mom_y            = -110.0
MOM_SIZE         = 55
MOM_TARGET_Y     = 95
mom_align_timer  = 0
MOM_ALIGN_NEED   = 90
flash_timer      = 0
reunion_timer    = 0
REUNION_DURATION = 300

PH2 = 0.25
PH3 = 0.55
PH4 = 0.80

crystals      = []
heart_pickups = []
flowers       = []

SPARKLE_COLORS = [
    PINK,YELLOW,ORANGE,PURPLE,LIGHT_PINK,
    (255,100,200),(200,100,255),(255,200,100),(220,80,180),
]
sparkles = [[
    random.randint(0,WIDTH), random.randint(0,HEIGHT),
    random.uniform(0.3,1.8), random.randint(1,3),
    random.choice(SPARKLE_COLORS),
] for _ in range(140)]

CLOUD_COL = (255,228,215)
clouds = [[
    float(random.randint(-200,WIDTH)), float(random.randint(30,HEIGHT-130)),
    random.randint(35,68), random.uniform(0.20,0.55),
] for _ in range(7)]


# ── Draw helpers ──────────────────────────────────────────────────────────────

def gtext(text, **kw):
    screen.draw.text(text, bold=True, **kw)

def arcade(text, cx, cy, size, color, shadow=(55,0,75)):
    for dx,dy in [(-2,-2),(2,-2),(-2,2),(2,2),(0,-2),(0,2),(-2,0),(2,0)]:
        gtext(text, center=(cx+dx,cy+dy), fontsize=size, color=shadow)
    gtext(text, center=(cx,cy), fontsize=size, color=color)

def soft_glow(x, y, radius, color):
    for i in range(2):
        r  = radius + (2-i)*6
        lo = (2-i)*50
        c  = (min(color[0]+lo,255), min(color[1]+lo,255), min(color[2]+lo,255))
        screen.draw.filled_circle((int(x),int(y)), r, c)

def draw_sunset_bg():
    strip_h=10; n=len(SUNSET_STRIPS)
    for i in range(HEIGHT//strip_h+1):
        t=i/(HEIGHT//strip_h); ci=t*(n-1); idx=min(int(ci),n-2); f=ci-idx
        c1,c2=SUNSET_STRIPS[idx],SUNSET_STRIPS[idx+1]
        col=(int(c1[0]+(c2[0]-c1[0])*f),int(c1[1]+(c2[1]-c1[1])*f),int(c1[2]+(c2[2]-c1[2])*f))
        screen.draw.filled_rect(pygame.Rect(0,i*strip_h,WIDTH,strip_h+1),col)

def draw_cloud(x,y,s):
    c=CLOUD_COL
    screen.draw.filled_circle((int(x),int(y)),s,c)
    screen.draw.filled_circle((int(x-s),int(y+s//3)),int(s*.75),c)
    screen.draw.filled_circle((int(x+s),int(y+s//3)),int(s*.75),c)
    screen.draw.filled_circle((int(x-s//2),int(y-s//3)),int(s*.65),c)
    screen.draw.filled_circle((int(x+s//2),int(y-s//3)),int(s*.65),c)
    screen.draw.filled_circle((int(x-int(s*1.4)),int(y+s//2)),int(s*.5),c)
    screen.draw.filled_circle((int(x+int(s*1.4)),int(y+s//2)),int(s*.5),c)

def heart_polygon(cx,cy,size):
    pts=[]; scale=size/16.0
    for i in range(48):
        t=2*math.pi*i/48
        hx=16*(math.sin(t)**3)
        hy=-(13*math.cos(t)-5*math.cos(2*t)-2*math.cos(3*t)-math.cos(4*t))
        pts.append((int(cx+hx*scale),int(cy+hy*scale)))
    return pts

def draw_heart_poly(cx,cy,size,color,glow=None):
    if glow:
        pygame.draw.polygon(screen.surface,glow,heart_polygon(cx,cy,size+5))
    pygame.draw.polygon(screen.surface,color,heart_polygon(cx,cy,size))

def draw_fire(x,y):
    f=(anim_timer%6)-3; fy=int(y+20)
    screen.draw.filled_circle((int(x+f),fy),9,(210,25,0))
    screen.draw.filled_circle((int(x-f),fy+8),6,ORANGE)
    screen.draw.filled_circle((int(x),fy+6),7,ORANGE)
    screen.draw.filled_circle((int(x+f//2),fy+15),5,YELLOW)
    screen.draw.filled_circle((int(x),fy+22),3,YELLOW)
    screen.draw.filled_circle((int(x-f//2),fy+19),3,(255,255,180))

def draw_gem(x,y,size,base):
    ix,iy,s=int(x),int(y),size; r,g,b=base
    tl=(ix-int(s*.55),iy-int(s*.85)); tr=(ix+int(s*.55),iy-int(s*.85))
    gl=(ix-s,iy+int(s*.05));           gr=(ix+s,iy+int(s*.05))
    mid=(ix,iy+int(s*.05));             bot=(ix,iy+int(s*1.25))
    c1=(min(r+110,255),min(g+45,255),min(b+130,255))
    c2=(min(r+65,255),min(g+22,255),min(b+80,255))
    c3=(min(r+30,255),min(g+10,255),min(b+45,255))
    sh=(215,165,255)
    pygame.draw.polygon(screen.surface,c1,[tl,tr,gr,gl])
    pygame.draw.polygon(screen.surface,c2,[gl,mid,bot])
    pygame.draw.polygon(screen.surface,c3,[gr,mid,bot])
    pygame.draw.polygon(screen.surface,sh,[tl,tr,gr,bot,gl],2)
    pygame.draw.line(screen.surface,WHITE,tl,tr,2)
    pygame.draw.line(screen.surface,sh,tl,bot,1)
    pygame.draw.line(screen.surface,sh,tr,bot,1)
    screen.draw.filled_circle((tl[0]+s//3,tl[1]+s//3),max(2,s//5),WHITE)

def draw_flower(x,y,size,petal_col):
    ix,iy=int(x),int(y)
    soft_glow(ix,iy,size+4,petal_col)
    for i in range(5):
        angle=2*math.pi*i/5-math.pi/2
        px=ix+int(math.cos(angle)*size*0.95)
        py=iy+int(math.sin(angle)*size*0.95)
        screen.draw.filled_circle((px,py),int(size*.72),petal_col)
    screen.draw.filled_circle((ix,iy),int(size*.55),YELLOW)
    screen.draw.filled_circle((ix,iy),int(size*.25),WHITE)

def draw_heart_pickup(x,y):
    soft_glow(int(x),int(y),14,(220,80,150))
    draw_heart_poly(x,y,12,PINK,LIGHT_PINK)

def draw_guide_strip(mx, player_y_pos, my):
    strip_w=54; sx=int(mx-strip_w//2)
    sy=int(my+MOM_SIZE*0.85); sh=int(player_y_pos-sy-PLAYER_RADIUS)
    if sh<=0: return
    surf=pygame.Surface((strip_w,sh),pygame.SRCALPHA)
    surf.fill((255,220,50,55)); screen.surface.blit(surf,(sx,sy))
    pygame.draw.line(screen.surface,YELLOW,(sx,sy),(sx,sy+sh),2)
    pygame.draw.line(screen.surface,YELLOW,(sx+strip_w,sy),(sx+strip_w,sy+sh),2)
    for dot_y in range(sy+18,sy+sh-10,28):
        screen.draw.filled_circle((int(mx),dot_y),3,YELLOW)

def draw_hud():
    cfg=level_cfg(level)
    bx,by,bw,bh=20,HEIGHT-35,220,18
    screen.draw.filled_rect(pygame.Rect(bx+2,by+2,bw,bh),(195,155,175))
    screen.draw.filled_rect(pygame.Rect(bx,by,bw,bh),(230,200,215))
    fill=int(bw*(life/MAX_LIFE))
    if fill>0:
        bc=HOT_PINK if life>50 else (ORANGE if life>25 else YELLOW)
        screen.draw.filled_rect(pygame.Rect(bx,by,fill,bh),bc)
    screen.draw.rect(pygame.Rect(bx,by,bw,bh),DARK_PURPLE)
    gtext('LIFE',topleft=(bx,by-20),fontsize=17,color=DEEP_PINK)
    dx,dy,dw,dh=WIDTH-240,HEIGHT-35,220,18
    screen.draw.filled_rect(pygame.Rect(dx+2,dy+2,dw,dh),(100,170,175))
    screen.draw.filled_rect(pygame.Rect(dx,dy,dw,dh),(190,228,230))
    dfill=int(dw*min(1.0,distance/cfg['dist_need']))
    if dfill>0: screen.draw.filled_rect(pygame.Rect(dx,dy,dfill,dh),TEAL)
    screen.draw.rect(pygame.Rect(dx,dy,dw,dh),DARK_PURPLE)
    gtext('DISTANCE TO MOM',topleft=(dx,dy-20),fontsize=17,color=DARK_PURPLE)
    gtext('SCORE: '+str(score),topright=(WIDTH-20,20),fontsize=22,color=DARK_TEXT)
    gtext('LEVEL: '+str(level),topleft=(20,20),fontsize=22,color=DARK_TEXT)

def draw_aim_arrow(px,tx,py):
    diff=tx-px
    if abs(diff)<50:
        r=18+int(4*math.sin(anim_timer*0.15))
        screen.draw.circle((int(px),int(py)-45),r,YELLOW); return
    ax,ay=int(px),int(py)-48
    if diff>0: pts=[(ax+18,ay),(ax+3,ay-10),(ax+3,ay+10)]
    else:       pts=[(ax-18,ay),(ax-3,ay-10),(ax-3,ay+10)]
    pygame.draw.polygon(screen.surface,YELLOW,pts)
    pygame.draw.polygon(screen.surface,(90,50,0),pts,2)

def alpha_overlay(color,alpha):
    surf=pygame.Surface((WIDTH,HEIGHT),pygame.SRCALPHA)
    surf.fill((*color,alpha)); screen.surface.blit(surf,(0,0))

def card(x,y,w,h):
    screen.draw.filled_rect(pygame.Rect(x,y,w,h),(255,238,248))
    screen.draw.filled_rect(pygame.Rect(x+2,y+2,w-4,h-4),(255,248,253))
    screen.draw.rect(pygame.Rect(x,y,w,h),DEEP_PINK)

def circle_hit(x1,y1,r1,x2,y2,r2):
    return math.sqrt((x1-x2)**2+(y1-y2)**2)<r1+r2

def start_level():
    global distance,crystal_timer,heart_timer,flower_timer
    global mom_y,mom_align_timer,flash_timer,reunion_timer,player_x,player_y
    player_x=float(WIDTH//2); player_y=float(HEIGHT-100)
    distance=0.0; crystal_timer=0; heart_timer=0; flower_timer=0
    mom_y=-110.0; mom_align_timer=0; flash_timer=0; reunion_timer=0
    crystals.clear(); heart_pickups.clear(); flowers.clear()

def reset_game():
    global life,score,level,game_state,invincible_timer,high_score
    high_score=max(high_score,score)
    life,score=MAX_LIFE,0; level=1; game_state='playing'; invincible_timer=0
    start_level()

def read_pico():
    """Read latest button state from Pico over serial."""
    global left_btn, right_btn
    if pico:
        try:
            while pico.in_waiting:
                line = pico.readline().decode().strip()
                if line.startswith('(') and ',' in line:
                    l, r = line[1:-1].split(',')
                    left_btn  = int(l) == 1
                    right_btn = int(r) == 1
        except:
            pass


# ── Update ────────────────────────────────────────────────────────────────────

def update():
    global player_x,player_y,crystal_timer,heart_timer,flower_timer,distance
    global life,score,high_score,game_state,invincible_timer,anim_timer
    global mom_x,mom_y,mom_align_timer,flash_timer,reunion_timer,level
    global left_btn,right_btn

    anim_timer+=1

    # Read Pico buttons every frame
    read_pico()

    for s in sparkles:
        s[1]+=s[2]
        if s[1]>HEIGHT: s[1]=0; s[0]=random.randint(0,WIDTH)
    for c in clouds:
        c[0]+=c[3]
        if c[0]>WIDTH+210: c[0]=-210.0; c[1]=float(random.randint(30,HEIGHT-130))

    # Pico buttons can also start / restart the game
    if game_state=='start' and (left_btn or right_btn):
        reset_game(); return
    if game_state=='game_over' and (left_btn or right_btn):
        reset_game(); return
    if game_state=='level_complete' and (left_btn or right_btn):
        level+=1; game_state='playing'; start_level(); return

    cfg=level_cfg(level)

    if game_state=='playing':
        # Keyboard OR Pico buttons both work
        if keyboard.left  or left_btn:  player_x=max(PLAYER_RADIUS,player_x-PLAYER_SPEED)
        if keyboard.right or right_btn: player_x=min(WIDTH-PLAYER_RADIUS,player_x+PLAYER_SPEED)
        distance+=1
        if distance>=cfg['dist_need']:
            game_state='mom_sequence'; mom_x=float(random.randint(100,WIDTH-100))
            mom_y=-110.0; crystals.clear(); heart_pickups.clear(); flowers.clear(); return
        crystal_timer+=1
        if crystal_timer>=cfg['crystal_rate']:
            crystal_timer=0
            crystals.append({'x':float(random.randint(35,WIDTH-35)),'y':-42.0,
                'size':random.randint(14,min(30,22+level)),'color':random.choice(GEM_COLORS),
                'speed':random.uniform(cfg['speed_min'],cfg['speed_max'])})
        for c in list(crystals):
            c['y']+=c['speed']
            if c['y']>HEIGHT+55: crystals.remove(c)
            elif (invincible_timer<=0 and
                  circle_hit(player_x,player_y,PLAYER_RADIUS-5,c['x'],c['y'],c['size']-4)):
                life-=20; score-=CRYSTAL_PENALTY; invincible_timer=90; crystals.remove(c)
                if life<=0:
                    life=0
                    high_score=max(high_score,score)
                    game_state='game_over'
        heart_timer+=1
        if heart_timer>=cfg['heart_rate']:
            heart_timer=0
            heart_pickups.append({'x':float(random.randint(35,WIDTH-35)),'y':-20.0})
        for h in list(heart_pickups):
            h['y']+=1.8
            if h['y']>HEIGHT+30: heart_pickups.remove(h)
            elif circle_hit(player_x,player_y,PLAYER_RADIUS,h['x'],h['y'],14):
                life=min(MAX_LIFE,life+25); heart_pickups.remove(h)
        flower_timer+=1
        if flower_timer>=cfg['flower_rate']:
            flower_timer=0
            flowers.append({'x':float(random.randint(35,WIDTH-35)),'y':-20.0,
                'petal_col':random.choice(FLOWER_PETAL_COLS),'size':random.randint(9,13)})
        for f in list(flowers):
            f['y']+=1.6
            if f['y']>HEIGHT+30: flowers.remove(f)
            elif circle_hit(player_x,player_y,PLAYER_RADIUS,f['x'],f['y'],f['size']+4):
                score+=FLOWER_SCORE; flowers.remove(f)
        if invincible_timer>0: invincible_timer-=1

    elif game_state=='mom_sequence':
        # Keyboard OR Pico buttons both work
        if keyboard.left  or left_btn:  player_x=max(PLAYER_RADIUS,player_x-PLAYER_SPEED)
        if keyboard.right or right_btn: player_x=min(WIDTH-PLAYER_RADIUS,player_x+PLAYER_SPEED)
        if mom_y<MOM_TARGET_Y: mom_y+=2.5
        if mom_y>=MOM_TARGET_Y:
            if abs(player_x-mom_x)<50:
                mom_align_timer+=1
                if mom_align_timer>=MOM_ALIGN_NEED: game_state='reunion'; reunion_timer=0
            else: mom_align_timer=max(0,mom_align_timer-1)

    elif game_state=='reunion':
        reunion_timer+=1
        prog=reunion_timer/REUNION_DURATION
        if prog<PH2:
            spd=0.055+prog*0.06
            player_x+=(mom_x-player_x)*spd
            player_y+=(mom_y+24-player_y)*spd
        else:
            mom_x+=(WIDTH/2-mom_x)*0.04
            mom_y+=(HEIGHT/2-mom_y)*0.04
            player_x+=(mom_x-player_x)*0.06
            player_y+=(mom_y+18-player_y)*0.04
        if reunion_timer>=REUNION_DURATION: flash_timer=40; game_state='level_complete'

    elif game_state=='level_complete':
        if flash_timer>0: flash_timer-=1


# ── Draw ──────────────────────────────────────────────────────────────────────

def draw():
    draw_sunset_bg()
    for cx,cy,cs,_ in clouds: draw_cloud(cx,cy,cs)
    for sx,sy,_,sz,sc in sparkles:
        screen.draw.filled_circle((int(sx),int(sy)),sz,sc)

    if game_state=='start':
        card(WIDTH//2-330,HEIGHT//2-240,660,478)
        arcade('ROCKET TO YOUR HEART',WIDTH//2,HEIGHT//2-192,36,YELLOW)
        for hx_off in [-80,0,80]:
            draw_heart_poly(WIDTH//2+hx_off,HEIGHT//2-148,14,HOT_PINK,ORANGE)
        lines=[
            ("Fly babycakes through the sky to find her mom!",     DARK_TEXT),
            ("Collect  FLOWERS  to add to your score (+50 each).", (120,0,130)),
            ("Dark DIAMONDS remove life AND lower your score.",     (160,0, 60)),
            ("Pink HEARTS restore your life — no score effect.",    (150,0, 80)),
            ("Use  LEFT  and  RIGHT  arrow keys to move.",          DARK_TEXT),
        ]
        for i,(line,col) in enumerate(lines):
            gtext(line,center=(WIDTH//2,HEIGHT//2-92+i*46),fontsize=21,color=col)
        if (anim_timer//30)%2==0:
            arcade('PRESS  LEFT  OR  RIGHT  TO  BEGIN',WIDTH//2,HEIGHT//2+162,26,HOT_PINK)
        if high_score>0:
            gtext('Best score: '+str(high_score),center=(WIDTH//2,HEIGHT//2+210),
                  fontsize=18,color=DARK_PURPLE)

    elif game_state=='playing':
        for f in flowers: draw_flower(f['x'],f['y'],f['size'],f['petal_col'])
        for h in heart_pickups: draw_heart_pickup(h['x'],h['y'])
        for c in crystals: draw_gem(c['x'],c['y'],c['size'],c['color'])
        if invincible_timer<=0 or (anim_timer%8)<4:
            draw_fire(player_x,player_y)
            draw_heart_poly(player_x,player_y,PLAYER_RADIUS,HOT_PINK,ORANGE)
        draw_hud()

    elif game_state=='mom_sequence':
        if mom_y>=MOM_TARGET_Y:
            draw_guide_strip(mom_x,player_y,mom_y)
        draw_heart_poly(mom_x,mom_y,MOM_SIZE+8,LIGHT_PINK)
        draw_heart_poly(mom_x,mom_y,MOM_SIZE,PINK,HOT_PINK)
        gtext('MOM',center=(int(mom_x),int(mom_y)-MOM_SIZE-14),fontsize=20,color=DEEP_PINK)
        if invincible_timer<=0 or (anim_timer%8)<4:
            draw_fire(player_x,player_y)
            draw_heart_poly(player_x,player_y,PLAYER_RADIUS,HOT_PINK,ORANGE)
        if mom_y>=MOM_TARGET_Y:
            draw_aim_arrow(player_x,mom_x,player_y)
            bw,bh=180,14; bx,by=WIDTH//2-bw//2,HEIGHT-55
            screen.draw.filled_rect(pygame.Rect(bx,by,bw,bh),(220,200,220))
            p2=int(bw*(mom_align_timer/MOM_ALIGN_NEED))
            if p2>0: screen.draw.filled_rect(pygame.Rect(bx,by,p2,bh),HOT_PINK)
            screen.draw.rect(pygame.Rect(bx,by,bw,bh),DARK_PURPLE)
            gtext('stay close!',center=(WIDTH//2,by-18),fontsize=16,color=DEEP_PINK)
        if (anim_timer//25)%2==0:
            arcade("FIND YOUR MOM!",WIDTH//2,HEIGHT//2+80,34,YELLOW)
        draw_hud()

    elif game_state=='reunion':
        prog=reunion_timer/REUNION_DURATION
        if prog<PH2:
            draw_heart_poly(mom_x,mom_y,MOM_SIZE+8,LIGHT_PINK)
            draw_heart_poly(mom_x,mom_y,MOM_SIZE,PINK,HOT_PINK)
            gtext('MOM',center=(int(mom_x),int(mom_y)-MOM_SIZE-14),fontsize=20,color=DEEP_PINK)
            draw_fire(player_x,player_y)
            draw_heart_poly(player_x,player_y,PLAYER_RADIUS,HOT_PINK,ORANGE)
            if (anim_timer//22)%2==0:
                gtext('almost there...',center=(WIDTH//2,HEIGHT//2+110),fontsize=24,color=DEEP_PINK)
        elif prog<PH3:
            sub=(prog-PH2)/(PH3-PH2)
            glow_r=int(sub*90)
            if glow_r>0:
                screen.draw.filled_circle((int(mom_x),int(mom_y)+15),glow_r,LIGHT_PINK)
            big=MOM_SIZE+int(8*math.sin(anim_timer*0.18))
            draw_heart_poly(mom_x,mom_y,big+8,LIGHT_PINK)
            draw_heart_poly(mom_x,mom_y,big,PINK,HOT_PINK)
            draw_heart_poly(player_x,player_y,PLAYER_RADIUS,HOT_PINK,ORANGE)
            if (anim_timer//20)%2==0:
                arcade('FOUND HER!',WIDTH//2,HEIGHT//2+95,38,YELLOW)
        elif prog<PH4:
            sub=(prog-PH3)/(PH4-PH3)
            glow_r=90+int(sub*60)
            screen.draw.filled_circle((int(mom_x),int(mom_y)+15),glow_r,LIGHT_PINK)
            big=MOM_SIZE+int(10*math.sin(anim_timer*0.2))
            draw_heart_poly(mom_x,mom_y,big+8,LIGHT_PINK)
            draw_heart_poly(mom_x,mom_y,big,PINK,HOT_PINK)
            draw_heart_poly(player_x,player_y,PLAYER_RADIUS,HOT_PINK,ORANGE)
            for i in range(8):
                angle=2*math.pi*i/8+anim_timer*0.04
                dist=30+sub*100
                draw_heart_poly(mom_x+math.cos(angle)*dist,
                                mom_y+math.sin(angle)*dist,
                                max(4,12-int(sub*7)),PINK)
            arcade('REUNITED!',WIDTH//2,HEIGHT//2+110,48,HOT_PINK)
        else:
            sub=(prog-PH4)/(1.0-PH4)
            alpha_overlay(HOT_PINK,int(sub*160))
            big=MOM_SIZE+15
            draw_heart_poly(mom_x,mom_y,big+8,LIGHT_PINK)
            draw_heart_poly(mom_x,mom_y,big,PINK,HOT_PINK)
            draw_heart_poly(player_x,player_y,PLAYER_RADIUS,HOT_PINK,ORANGE)
            for i in range(10):
                angle=2*math.pi*i/10+anim_timer*0.03
                dist=50+sub*120
                draw_heart_poly(mom_x+math.cos(angle)*dist,
                                mom_y+math.sin(angle)*dist,
                                max(3,10-int(sub*6)),PINK)
            arcade('REUNITED!',WIDTH//2,HEIGHT//2+110,48,HOT_PINK)
        draw_hud()

    elif game_state=='level_complete':
        if flash_timer>0: alpha_overlay(HOT_PINK,min(165,flash_timer*4))
        card(WIDTH//2-255,HEIGHT//2-140,510,280)
        draw_heart_poly(WIDTH//2-42,HEIGHT//2-108,16,HOT_PINK,ORANGE)
        draw_heart_poly(WIDTH//2+42,HEIGHT//2-108,22,PINK,LIGHT_PINK)
        arcade('LEVEL '+str(level)+' COMPLETE!',WIDTH//2,HEIGHT//2-60,40,HOT_PINK)
        gtext('Score: '+str(score),center=(WIDTH//2,HEIGHT//2+5),fontsize=24,color=DARK_TEXT)
        gtext('Get ready for Level '+str(level+1)+'!',
              center=(WIDTH//2,HEIGHT//2+42),fontsize=20,color=PURPLE)
        if (anim_timer//30)%2==0:
            arcade('PRESS  LEFT  OR  RIGHT  TO  CONTINUE',WIDTH//2,HEIGHT//2+105,22,YELLOW)

    elif game_state=='game_over':
        for f in flowers: draw_flower(f['x'],f['y'],f['size'],f['petal_col'])
        for h in heart_pickups: draw_heart_pickup(h['x'],h['y'])
        for c in crystals: draw_gem(c['x'],c['y'],c['size'],c['color'])
        draw_hud()
        card(WIDTH//2-235,HEIGHT//2-125,470,250)
        arcade('GAME OVER',WIDTH//2,HEIGHT//2-65,54,HOT_PINK)
        gtext('Score: '+str(score),center=(WIDTH//2,HEIGHT//2+5),fontsize=26,color=DARK_TEXT)
        gtext('Best:  '+str(high_score),center=(WIDTH//2,HEIGHT//2+38),fontsize=22,color=PURPLE)
        if (anim_timer//30)%2==0:
            arcade('PRESS  LEFT  OR  RIGHT  TO  RESTART',WIDTH//2,HEIGHT//2+90,24,PURPLE)


def on_key_down(key):
    global game_state,level
    if   game_state=='start'          and key in (keys.LEFT, keys.RIGHT): reset_game()
    elif game_state=='level_complete' and key in (keys.LEFT, keys.RIGHT):
        level+=1; game_state='playing'; start_level()
    elif game_state=='game_over'      and key in (keys.LEFT, keys.RIGHT): reset_game()


pgzrun.go()