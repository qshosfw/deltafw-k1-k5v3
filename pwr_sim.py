
def calculate_pwr(cal, power_index, band):
    dividers_band2 = [ 20, 15, 10, 8, 6, 4 ]
    dividers_band5 = [ 25, 19, 13, 9, 6, 4 ]
    
    dividers = dividers_band2 if band == 2 else dividers_band5
    
    currentPower = power_index
    # mimics currentPower-- logic
    currentPower -= 1
    
    if currentPower < 6:
        txp = (cal * (3 if currentPower == 5 else 4)) // dividers[currentPower]
    else:
        txp = cal + 24
        
    return min(255, txp)

powers = ["LOW1", "LOW2", "LOW3", "LOW4", "LOW5", "MID", "HIGH"]
indices = [1, 2, 3, 4, 5, 6, 7]

bands = [2, 5]
cal_values = [100, 150, 200, 255]

for band in bands:
    print(f"Band {band}:")
    for cal in cal_values:
        print(f"  Cal {cal}:")
        for i, name in zip(indices, powers):
            pwr = calculate_pwr(cal, i, band)
            print(f"    {name:5}: {pwr}")
