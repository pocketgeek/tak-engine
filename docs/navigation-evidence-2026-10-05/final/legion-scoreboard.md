# Legion scoreboard (mean over seeds; 2% tolerance)

| metric | legion >= best | legion worse |
|---|---|---|
| crossed_middle | 94 | 15 |
| arrived_settled | 100 | 9 |
| spin_unit_ticks | 109 | 0 |
| class_terrain_stuck_unit_ticks | 105 | 4 |
| final_terrain_stuck | 108 | 1 |
| final_open_idle | 93 | 16 |
| trapped_units_moving_after_grace | 91 | 18 |
| path_optimality_ratio_mean | 17 | 0 |

## Cases where Legion is worse than the best other mode

| scenario | population | ticks | metric | legion | best other |
|---|---|---|---|---|---|
| bridges | 500x1@100 | 6000 | crossed_middle | 311.0 | 404.3 (flowfield) |
| bridges | 500x1@100 | 6000 | arrived_settled | 145.0 | 190.7 (flowfield) |
| bridges | 1000x1@100 | 6000 | crossed_middle | 332.3 | 442.0 (flowfield) |
| bridges | 1000x1@100 | 6000 | arrived_settled | 192.3 | 238.7 (flowfield) |
| bridges | 2000x1@100 | 6000 | crossed_middle | 364.0 | 380.3 (cooperative) |
| crowdtrap | 200x1@100 | 6000 | crossed_middle | 191.0 | 195.0 (cooperative) |
| crowdtrap | 200x1@100 | 6000 | arrived_settled | 99.3 | 123.0 (retail-plus) |
| crowdtrap | 250x8@50 | 6000 | final_terrain_stuck | 1.3 | 0.3 (flowfield) |
| doors | 200x1@100 | 6000 | final_open_idle | 1.3 | 0.7 (retail-plus) |
| doors | 250x8@50 | 6000 | final_open_idle | 11.3 | 9.7 (retail-plus) |
| doors | 500x1@100 | 6000 | crossed_middle | 370.7 | 419.3 (flowfield) |
| doors | 500x1@100 | 6000 | arrived_settled | 153.0 | 197.0 (flowfield) |
| doors | 1000x1@100 | 6000 | crossed_middle | 337.7 | 454.0 (flowfield) |
| doors | 1000x1@100 | 6000 | arrived_settled | 214.7 | 233.3 (flowfield) |
| doors | 2000x1@100 | 12000 | arrived_settled | 438.7 | 486.7 (cooperative) |
| exploration | 200x1@100 | 6000 | final_open_idle | 0.3 | 0.0 (flowfield) |
| exploration | 250x8@50 | 6000 | final_open_idle | 8.3 | 5.7 (flowfield) |
| groupdetour | 500x1@100 | 6000 | crossed_middle | 488.0 | 500.0 (cooperative) |
| groupdetour | 1000x1@100 | 6000 | crossed_middle | 627.0 | 712.7 (cooperative) |
| groupdetour | 2000x1@100 | 6000 | crossed_middle | 1,069.3 | 1,168.0 (cooperative) |
| jagged | 200x1@100 | 6000 | final_open_idle | 5.7 | 1.0 (flowfield) |
| jagged | 250x8@50 | 6000 | crossed_middle | 531.3 | 1,000.0 (flowfield) |
| jagged | 250x8@50 | 6000 | arrived_settled | 463.0 | 926.0 (retail-plus) |
| jagged | 250x8@50 | 6000 | class_terrain_stuck_unit_ticks | 7,818.0 | 6,501.3 (retail) |
| jagged | 250x8@50 | 6000 | final_open_idle | 96.0 | 4.0 (retail-plus) |
| jagged | 500x1@100 | 6000 | final_open_idle | 12.0 | 6.3 (cooperative) |
| jagged | 1000x1@100 | 6000 | crossed_middle | 799.3 | 819.0 (flowfield) |
| maze | 250x8@50 | 6000 | final_open_idle | 8.3 | 4.0 (flowfield) |
| open | 250x8@50 | 6000 | final_open_idle | 0.7 | 0.0 (retail) |
| open | 500x4@100 | 6000 | final_open_idle | 2.0 | 0.0 (retail) |
| open | 2000x1@100 | 6000 | final_open_idle | 0.3 | 0.0 (retail) |
| opposingcolumns | 200x1@100 | 6000 | final_open_idle | 3.7 | 3.3 (cooperative) |
| opposingcolumns | 250x8@50 | 6000 | crossed_middle | 678.3 | 1,000.0 (retail) |
| opposingcolumns | 250x8@50 | 6000 | arrived_settled | 317.3 | 724.7 (retail-plus) |
| opposingcolumns | 500x1@100 | 6000 | crossed_middle | 483.0 | 499.0 (retail) |
| opposingcolumns | 500x4@100 | 6000 | final_open_idle | 14.3 | 10.0 (cooperative) |
| opposingcolumns | 1000x1@100 | 6000 | final_open_idle | 5.3 | 5.0 (cooperative) |
| opposingcolumns | 2000x1@100 | 6000 | crossed_middle | 991.0 | 1,144.7 (flowfield) |
| opposingcolumns | 2000x1@100 | 12000 | crossed_middle | 1,185.0 | 1,298.0 (flowfield) |
| recovery | 200x1@100 | 6000 | trapped_units_moving_after_grace | 200.0 | 0.0 (cooperative) |
| recovery | 250x8@50 | 6000 | trapped_units_moving_after_grace | 1,000.0 | 0.0 (cooperative) |
| recovery | 500x1@100 | 6000 | trapped_units_moving_after_grace | 500.0 | 0.0 (cooperative) |
| recovery | 500x4@100 | 6000 | trapped_units_moving_after_grace | 2,000.0 | 0.0 (cooperative) |
| recovery | 1000x1@100 | 6000 | trapped_units_moving_after_grace | 1,000.0 | 0.0 (cooperative) |
| recovery | 2000x1@100 | 6000 | trapped_units_moving_after_grace | 2,000.0 | 0.0 (cooperative) |
| recovery-passive | 200x1@100 | 6000 | trapped_units_moving_after_grace | 200.0 | 0.0 (cooperative) |
| recovery-passive | 250x8@50 | 6000 | trapped_units_moving_after_grace | 1,000.0 | 0.0 (cooperative) |
| recovery-passive | 500x1@100 | 6000 | trapped_units_moving_after_grace | 500.0 | 0.0 (cooperative) |
| recovery-passive | 500x4@100 | 6000 | trapped_units_moving_after_grace | 2,000.0 | 0.0 (cooperative) |
| recovery-passive | 1000x1@100 | 6000 | trapped_units_moving_after_grace | 1,000.0 | 0.0 (cooperative) |
| recovery-passive | 2000x1@100 | 6000 | trapped_units_moving_after_grace | 2,000.0 | 0.0 (cooperative) |
| sharedgoal | 500x1@100 | 6000 | final_open_idle | 31.0 | 4.0 (retail) |
| singleunit | 200x1@100 | 6000 | class_terrain_stuck_unit_ticks | 5,830.3 | 0.0 (cooperative) |
| singleunit | 500x1@100 | 6000 | class_terrain_stuck_unit_ticks | 5,830.3 | 54.0 (cooperative) |
| singleunit | 1000x1@100 | 6000 | arrived_settled | 619.7 | 1,000.0 (cooperative) |
| singleunit | 1000x1@100 | 6000 | final_open_idle | 331.0 | 0.0 (cooperative) |
| trapped | 200x1@100 | 6000 | class_terrain_stuck_unit_ticks | 105.0 | 81.3 (retail) |
| unreachable | 200x1@100 | 6000 | trapped_units_moving_after_grace | 200.0 | 0.0 (cooperative) |
| unreachable | 250x8@50 | 6000 | trapped_units_moving_after_grace | 1,000.0 | 0.0 (cooperative) |
| unreachable | 500x1@100 | 6000 | trapped_units_moving_after_grace | 500.0 | 0.0 (cooperative) |
| unreachable | 500x4@100 | 6000 | trapped_units_moving_after_grace | 2,000.0 | 0.0 (cooperative) |
| unreachable | 1000x1@100 | 6000 | trapped_units_moving_after_grace | 1,000.0 | 0.0 (cooperative) |
| unreachable | 2000x1@100 | 6000 | trapped_units_moving_after_grace | 2,000.0 | 0.0 (cooperative) |
