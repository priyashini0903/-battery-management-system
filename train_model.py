
import pandas as pd
from sklearn.model_selection import train_test_split
from sklearn.ensemble import RandomForestRegressor
import joblib

df = pd.read_csv('battery_data.csv')

X = df[['Voltage', 'Current', 'Temp', 'Humidity']]
y_soc = df['SOC']
y_soh = df['SOH']

X_train, X_test, y_soc_train, y_soc_test = train_test_split(X, y_soc, test_size=0.2, random_state=42)
X_train2, X_test2, y_soh_train, y_soh_test = train_test_split(X, y_soh, test_size=0.2, random_state=42)

soc_model = RandomForestRegressor(n_estimators=100, random_state=42)
soh_model = RandomForestRegressor(n_estimators=100, random_state=42)

soc_model.fit(X_train, y_soc_train)
soh_model.fit(X_train2, y_soh_train)

joblib.dump(soc_model, 'soc_model.pkl')
joblib.dump(soh_model, 'soh_model.pkl')

print("✅ SOC and SOH models trained and saved successfully!")
