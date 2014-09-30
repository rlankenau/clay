Type Spark
	Field wire:Wire
	Field n:Int
	Field arrived:Int
	
	Function Make:Spark( wire:Wire )
		Local spark:Spark = New Spark
		spark.wire = wire
		wire.b.done = False
		
		For Local other:Spark = EachIn patch.sparks
			If other.wire = wire
				patch.sparks.Remove( other )
			EndIf
		Next
		
		Return spark
	End Function
	
	Method Update()
		If Not arrived
			n = n + 2
			
			If n >= 30
				n = 30
				arrived = True
				Local satisfied:Byte[ wire.b.ins ]
				satisfied[ wire.bId ] = True
				
				For Local n:Int = 0 Until wire.b.ins
					For Local other:Wire = EachIn patch.wires
						If other.b = wire.b And other <> wire
							satisfied[n] = other.a.done
						EndIf
					Next
				Next
				
				For Local spark:Spark = EachIn patch.sparks
					If spark.wire.b = wire.b
						If Not spark.arrived
							satisfied[ spark.wire.bId ] = False
						EndIf
					EndIf
				Next
				
				Local shoot:Int = True
				
				For Local n:Int = 0 Until wire.b.ins
					If Not satisfied[n]
						shoot = False
						Exit
					EndIf
				Next
				
				If shoot
					For Local spark:Spark = EachIn patch.sparks
						If spark.wire.b = wire.b
							patch.sparks.Remove( spark )
						EndIf
					Next
					
					wire.b.Execute()
				EndIf
			EndIf
		EndIf
	End Method
	
	Method Render()
		Local x0:Int = wire.a.x + 3
		Local y0:Int = wire.a.y + wire.a.h - 2
		Local x1:Int = wire.b.x + 3 + wire.bId * wire.b.gap
		Local y1:Int = wire.b.y + 1
		Local x:Float, y:Float
		Local t:Float = n / 30.0
		x = x1 * t + x0 * ( 1.0 - t )
		y = y1 * t + y0 * ( 1.0 - t )
		SetColor 255, 255, 0
		DrawOval x - 2, y - 2, 5, 5
	End Method
End Type