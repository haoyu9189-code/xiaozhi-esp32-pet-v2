#!/usr/bin/env python3
import os
os.chdir(r'd:\xiaozhi\xiaozhi-esp32-main')
from PIL import Image
import numpy as np

files = [
    'gifs/dynamic1/Default_dynamic1_1.png',
    'gifs/dynamic2/Default_dynamic2_1.png',
    'gifs/dynamic3/Default_dynamic3_1.png',
    'gifs/static1/Default_static1_1.png'
]

def calc_similarity(arr1, arr2):
    diff = arr1.astype(np.float32) - arr2.astype(np.float32)
    mse = np.mean(diff ** 2)
    return np.exp(-mse / 5000)

images = {}
for f in files:
    img = Image.open(f).convert('RGB')
    images[f] = np.array(img)

print('首图相似度比较:')
for i, f1 in enumerate(files):
    for f2 in files[i+1:]:
        sim = calc_similarity(images[f1], images[f2])
        name1 = f1.split('/')[1]
        name2 = f2.split('/')[1]
        status = "相同" if sim > 0.998 else "不同"
        print(f'  {name1} vs {name2}: {sim*100:.2f}% ({status})')
