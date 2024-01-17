from joblib import load
import numpy as np
from datetime import datetime

def get_forecast_for_date(target_date):
    # Load the model
    model_results = load("sarima_weekly_model.pkl")

    # Determine the last date in the model's training data
    last_date_in_data = model_results.data.orig_endog.index[-1]

    # Calculate the difference in weeks between the target date and the last date in the data
    weeks_difference = (target_date - last_date_in_data).days // 7

    # Generate forecasts
    forecast = model_results.get_forecast(steps=weeks_difference)
    mean_forecast = forecast.predicted_mean

    # Return the forecast for the target date
    return mean_forecast.iloc[-1]

if __name__ == "__main__":
    # Ask user for a date
    date_input = input("Enter a date in the format YYYYMMDD: ")
    target_date = datetime.strptime(date_input, "%Y%m%d")

    expected_temp = get_forecast_for_date(target_date)
    print(f"The expected temperature on {target_date.strftime('%Y-%m-%d')} is {expected_temp:.2f}°C.")
