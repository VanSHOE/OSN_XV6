# %%
# Import stuff
 
import numpy as np
import pandas as pd
import matplotlib.pyplot as plt
import csv 

# %%
# Get data

proc3 = {}
proc4 = {}
proc5 = {}
proc6 = {}
proc7 = {}
proc8 = {}
proc9 = {}
proc10 = {}
# each of these has ticks as key and queue as value

# in procdump.csv, first column in process number, second is queue, third is ticks
with open('procdump.csv', 'r') as csvfile:
    reader = csv.reader(csvfile, delimiter=',')
    # remove blank lines
    reader = filter(None, reader)
    for row in reader:
        if row[0] == '3':
            proc3[int(row[2])] = int(row[1])
        elif row[0] == '4':
            proc4[int(row[2])] = int(row[1])
        elif row[0] == '5':
            proc5[int(row[2])] = int(row[1])
        elif row[0] == '6':
            proc6[int(row[2])] = int(row[1])
        elif row[0] == '7':
            proc7[int(row[2])] = int(row[1])
        elif row[0] == '8':
            proc8[int(row[2])] = int(row[1])
        elif row[0] == '9':
            proc9[int(row[2])] = int(row[1])
        elif row[0] == '10':
            proc10[int(row[2])] = int(row[1])

# %%
# Make graph

# width should be twice the height of graph
h, w = plt.figaspect(2)
plt.figure(dpi = 600, facecolor='#1c1d27', figsize=(w,h))
ax = plt.axes()
ax.set_facecolor('#1c1d27')
plt.plot(proc3.keys(), proc3.values(), label = 'P1', color="#c678dd", linewidth=0.6)
plt.plot(proc4.keys(), proc4.values(), label = 'P2', color="#e5c07b", linewidth=0.6)
plt.plot(proc5.keys(), proc5.values(), label = 'P3', color="#e06c75", linewidth=0.6)
plt.plot(proc6.keys(), proc6.values(), label = 'P4', color="#56b6c2", linewidth=0.6)
plt.plot(proc7.keys(), proc7.values(), label = 'P5', color="#98c379", linewidth=0.6)
plt.plot(proc8.keys(), proc8.values(), label = 'P6', color="#61afef", linewidth=0.6)
plt.plot(proc9.keys(), proc9.values(), label = 'P7', color="#d19a66", linewidth=0.6)
plt.plot(proc10.keys(), proc10.values(), label = 'P8', color="#abb2bf", linewidth=0.6)
plt.xlabel('Ticks', fontsize=8, color='#abb2bf', font = 'Consolas', labelpad = 5)
plt.ylabel('Queue', fontsize=8, color='#abb2bf', font = 'Consolas', labelpad = 5)
plt.xticks(fontsize = 8, color='#abb2bf', font = 'Consolas')
plt.yticks(fontsize = 8, color='#abb2bf', font = 'Consolas')
ax.spines['bottom'].set_color('#abb2bf')
ax.spines['top'].set_color('#abb2bf')
ax.spines['left'].set_color('#abb2bf')
ax.spines['right'].set_color('#abb2bf')
ax.tick_params(axis='x', colors='#abb2bf')
ax.set_yticks([0, 1, 2, 3, 4])
ax.tick_params(axis='y', colors='#abb2bf')
plt.rcParams['axes.facecolor'] = '#1c1d27'
plt.rcParams['axes.titlecolor'] = '#abb2bf'
plt.rcParams['axes.edgecolor'] = '#abb2bf'
plt.rcParams['axes.labelcolor'] = '#abb2bf'
plt.rcParams['figure.facecolor'] = '#1c1d27'
plt.rcParams['legend.labelcolor'] = '#abb2bf'
plt.rcParams['xtick.color'] = '#1bb2bf'
plt.rcParams['ytick.color'] = '#1bb2bf'
plt.rcParams['text.color'] = '#abb2bf'
plt.rcParams['xtick.labelcolor'] = '#abb2bf'
plt.rcParams['ytick.labelcolor'] = '#abb2bf'
plt.rcParams['xtick.top'] = True
plt.rcParams['xtick.bottom'] = True
plt.rcParams['ytick.left'] = True
plt.rcParams['ytick.right'] = True
plt.legend(loc = 'lower right', prop={'size': 6}, facecolor='#1c1d27', edgecolor='#1c1d27', framealpha=1)
plt.title('MLFQ Scheduling Graph', fontsize=10, color='#abb2bf', font = 'Consolas', pad = 10)
# plt.show()
# # save as png
plt.savefig('graph.png', dpi = 600)

# %%
