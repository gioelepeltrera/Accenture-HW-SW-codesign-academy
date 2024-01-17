import pandas as pd
from statsmodels.tsa.statespace.sarimax import SARIMAX

# 1. Load the data from the CSV file
data = pd.read_csv('./open-meteo-45.10N7.70E239m.csv')

# Preprocessing
data = data.drop(0).reset_index(drop=True)
data.columns = data.iloc[0]
data = data.drop(0)
data['time'] = pd.to_datetime(data['time'])
data.set_index('time', inplace=True)
data['temperature_2m (°C)'] = pd.to_numeric(data['temperature_2m (°C)'])

# Create a dataset with weekly average, maximum, and minimum temperatures
weekly_data = data['temperature_2m (°C)'].resample('W').agg(['mean', 'max', 'min'])

# Handle any missing values by forward filling
weekly_data.ffill(inplace=True)

# 2. Adjust SARIMA parameters for yearly seasonality with weekly data (52 weeks)
p, d, q = 1, 1, 1
P, D, Q, S = 1, 1, 1, 52  # 52 weeks in a year

# Function to train and save SARIMA model for a given column
def train_and_save_sarima(column_name):
    sarima_model = SARIMAX(weekly_data[column_name], order=(p, d, q), seasonal_order=(P, D, Q, S))
    sarima_result = sarima_model.fit(disp=False, maxiter=500)
    print(f"\nModel Summary for {column_name}:\n")
    print(sarima_result.summary())
    sarima_result.save(f'sarima_weekly_{column_name}_model.pkl')
    print(f"\n{column_name.capitalize()} temperature model saved as sarima_weekly_{column_name}_model.pkl\n")

# 3. Train and save SARIMA models for mean, max, and min temperatures
for column in ['mean', 'max', 'min']:
    train_and_save_sarima(column)
